/*
** binding-mruby.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 Jonas Kulla <Nyocurio@gmail.com>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "binding.h"

#include "mruby.h"
#include "mruby/string.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/irep.h"
#include "mruby/compile.h"
#include "mruby/proc.h"
#include "mruby/dump.h"

#include "binding-util.h"

#include "stdio.h"
#include "zlib.h"

#include "SDL2/SDL_messagebox.h"
#include "SDL2/SDL_rwops.h"
#include "SDL2/SDL_timer.h"

#include "globalstate.h"
#include "texpool.h"
#include "eventthread.h"
#include "filesystem.h"

#include "binding-types.h"
#include "mrb-ext/marshal.h"

void mrbBindingExecute();
void mrbBindingTerminate();

ScriptBinding scriptBindingImpl =
{
    mrbBindingExecute,
    mrbBindingTerminate
};

ScriptBinding *scriptBinding = &scriptBindingImpl;


void fileBindingInit(mrb_state *);
void timeBindingInit(mrb_state *);
void marshalBindingInit(mrb_state *);
void kernelBindingInit(mrb_state *);

void tableBindingInit(mrb_state *);
void etcBindingInit(mrb_state *);
void fontBindingInit(mrb_state *);
void bitmapBindingInit(mrb_state *);
void spriteBindingInit(mrb_state *);
void planeBindingInit(mrb_state *);
void viewportBindingInit(mrb_state *);
void windowBindingInit(mrb_state *);
void tilemapBindingInit(mrb_state *);

void inputBindingInit(mrb_state *);
void audioBindingInit(mrb_state *);
void graphicsBindingInit(mrb_state *);

/* From module_rpg.c */
extern const uint8_t mrbModuleRPG[];

static void mrbBindingInit(mrb_state *mrb)
{
	int arena = mrb_gc_arena_save(mrb);

	/* Init standard classes */
	fileBindingInit(mrb);
	timeBindingInit(mrb);
	marshalBindingInit(mrb);
	kernelBindingInit(mrb);

	/* Init RGSS classes */
	tableBindingInit(mrb);
	etcBindingInit(mrb);
	fontBindingInit(mrb);
	bitmapBindingInit(mrb);
	spriteBindingInit(mrb);
	planeBindingInit(mrb);
	viewportBindingInit(mrb);
	windowBindingInit(mrb);
	tilemapBindingInit(mrb);

	/* Init RGSS modules */
	inputBindingInit(mrb);
	audioBindingInit(mrb);
	graphicsBindingInit(mrb);

	/* Load RPG module */
	mrb_load_irep(mrb, mrbModuleRPG);

	mrb_define_global_const(mrb, "MKXP", mrb_true_value());

	mrb_gc_arena_restore(mrb, arena);
}

static mrb_value
mkxpTimeOp(mrb_state *mrb, mrb_value)
{
	mrb_int iterations = 1;
	const char *opName = "";
	mrb_value block;

	mrb_get_args(mrb, "|iz&", &iterations, &opName, &block);

	Uint64 start = SDL_GetPerformanceCounter();

	for (int i = 0; i < iterations; ++i)
		mrb_yield(mrb, block, mrb_nil_value());

	Uint64 diff = SDL_GetPerformanceCounter() - start;
	double avg = (double) diff / iterations;

	double sec = avg / SDL_GetPerformanceFrequency();
	float ms = sec * 1000;

	printf("<%s> [%f ms]\n", opName, ms);
	fflush(stdout);

	return mrb__float_value(ms);
}

static const char *
mrbValueString(mrb_value value)
{
	return mrb_string_p(value) ? RSTRING_PTR(value) : 0;
}

static void
showExcMessageBox(mrb_state *mrb, mrb_value exc)
{
	/* Display actual exception in a message box */
	mrb_value mesg = mrb_funcall(mrb, exc, "message", 0);
	mrb_value line = mrb_attr_get(mrb, exc, mrb_intern2(mrb, "line", 4));
	mrb_value file = mrb_attr_get(mrb, exc, mrb_intern2(mrb, "file", 4));
	const char *excClass = mrb_class_name(mrb, mrb_class(mrb, exc));

	char msgBoxText[512];
	snprintf(msgBoxText, 512, "Script '%s' line %d: %s occured.\n\n%s",
	         mrbValueString(file), mrb_fixnum(line), excClass, mrbValueString(mesg));

	gState->eThread().showMessageBox(msgBoxText, SDL_MESSAGEBOX_ERROR);
}

static void
checkException(mrb_state *mrb)
{
	if (!mrb->exc)
		return;

	mrb_value exc = mrb_obj_value(mrb->exc);
	MrbData &mrbData = *getMrbData(mrb);

	/* Check if an actual exception occured, or just a shutdown was called */
	if (mrb_obj_class(mrb, exc) != mrbData.exc[Shutdown])
		showExcMessageBox(mrb, exc);
}


static void
showError(const QByteArray &msg)
{
	gState->eThread().showMessageBox(msg.constData());
}

static void
runCustomScript(mrb_state *mrb, mrbc_context *ctx, const char *filename)
{
	/* Execute custom script */
	FILE *f = fopen(filename, "r");

	if (!f)
	{
		static char buffer[256];
		snprintf(buffer, sizeof(buffer), "Unable to open script '%s'", filename);
		showError(buffer);

		return;
	}

	ctx->filename = strdup(filename);
	ctx->lineno = 1;

	/* Run code */
	mrb_load_file_cxt(mrb, f, ctx);

	free(ctx->filename);
	fclose(f);
}

static void
runMrbFile(mrb_state *mrb, const char *filename)
{
	/* Execute compiled script */
	FILE *f = fopen(filename, "r");

	if (!f)
	{
		static char buffer[256];
		snprintf(buffer, sizeof(buffer), "Unable to open compiled script '%s'", filename);
		showError(buffer);

		return;
	}

	int n = mrb_read_irep_file(mrb, f);

	if (n < 0)
	{
		static char buffer[256];
		snprintf(buffer, sizeof(buffer), "Unable to read compiled script '%s'", filename);
		showError(buffer);

		return;
	}

	mrb_run(mrb, mrb_proc_new(mrb, mrb->irep[n]), mrb_top_self(mrb));

	fclose(f);
}

static void
runRMXPScripts(mrb_state *mrb, mrbc_context *ctx)
{
	const QByteArray &scriptPack = gState->rtData().config.game.scripts;

	if (scriptPack.isEmpty())
	{
		showError("No game scripts specified (missing Game.ini?)");
		return;
	}

	if (!gState->fileSystem().exists(scriptPack.constData()))
	{
		showError("Unable to open '" + scriptPack + "'");
		return;
	}

	/* We use a secondary util state to unmarshal the scripts */
	mrb_state *scriptMrb = mrb_open();
	SDL_RWops ops;

	gState->fileSystem().openRead(ops, scriptPack.constData());

	mrb_value scriptArray = marshalLoadInt(scriptMrb, &ops);
	SDL_RWclose(&ops);

	if (!mrb_array_p(scriptArray))
	{
		showError("Failed to read script data");
		mrb_close(scriptMrb);
		return;
	}

	int scriptCount = mrb_ary_len(scriptMrb, scriptArray);

	QByteArray decodeBuffer;
	decodeBuffer.resize(0x1000);

	for (int i = 0; i < scriptCount; ++i)
	{
		mrb_value script = mrb_ary_entry(scriptArray, i);

		mrb_value scriptChksum = mrb_ary_entry(script, 0);
		mrb_value scriptName   = mrb_ary_entry(script, 1);
		mrb_value scriptString = mrb_ary_entry(script, 2);

		(void) scriptChksum;

		int result = Z_OK;
		ulong bufferLen;

		while (true)
		{
			unsigned char *bufferPtr =
			        reinterpret_cast<unsigned char*>(const_cast<char*>(decodeBuffer.constData()));
			unsigned char *sourcePtr =
			        reinterpret_cast<unsigned char*>(RSTRING_PTR(scriptString));

			bufferLen = decodeBuffer.length();

			result = uncompress(bufferPtr, &bufferLen,
			                    sourcePtr, RSTRING_LEN(scriptString));

			bufferPtr[bufferLen] = '\0';

			if (result != Z_BUF_ERROR)
				break;

			decodeBuffer.resize(decodeBuffer.size()*2);
		}

		if (result != Z_OK)
		{
			static char buffer[256];
			snprintf(buffer, sizeof(buffer), "Error decoding script %d: '%s'",
			         i, RSTRING_PTR(scriptName));

			showError(buffer);
			break;
		}

		ctx->filename = RSTRING_PTR(scriptName);
		ctx->lineno = 1;

		int ai = mrb_gc_arena_save(mrb);

		/* Execute code */
		mrb_load_nstring_cxt(mrb, decodeBuffer.constData(), bufferLen, ctx);

		mrb_gc_arena_restore(mrb, ai);

		if (mrb->exc)
			break;
	}

	mrb_close(scriptMrb);
}

void mrbBindingExecute()
{
	mrb_state *mrb = mrb_open();

	gState->setBindingData(mrb);

	MrbData mrbData(mrb);
	mrb->ud = &mrbData;

	mrb_define_module_function(mrb, mrb->kernel_module, "time_op",
	                           mkxpTimeOp, MRB_ARGS_OPT(2) | MRB_ARGS_BLOCK());

	mrbBindingInit(mrb);

	mrbc_context *ctx = mrbc_context_new(mrb);
	ctx->capture_errors = 1;

	Config &conf = gState->rtData().config;
	QByteArray &customScript = conf.customScript;
	QByteArray mrbFile = conf.bindingConf.value("mrbFile").toByteArray();

	if (!customScript.isEmpty())
		runCustomScript(mrb, ctx, customScript.constData());
	else if (!mrbFile.isEmpty())
		runMrbFile(mrb, mrbFile.constData());
	else
		runRMXPScripts(mrb, ctx);

	checkException(mrb);

	gState->rtData().rqTermAck = true;
	gState->texPool().disable();

	mrbc_context_free(mrb, ctx);
	mrb_close(mrb);
}

void mrbBindingTerminate()
{
	mrb_state *mrb = static_cast<mrb_state*>(gState->bindingData());
	MrbData *data = static_cast<MrbData*>(mrb->ud);

	mrb_raise(mrb, data->exc[Shutdown], "");
}