/*
** glstate.cpp
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

#include "glstate.h"
#include "GL/glew.h"
#include "SDL2/SDL_rect.h"
#include "etc.h"

void GLClearColor::apply(const Vec4 &value)
{
	glClearColor(value.x, value.y, value.z, value.w);
}

void GLScissorBox::apply(const IntRect &value)
{
	glScissor(value.x, value.y, value.w, value.h);
}

void GLScissorBox::setIntersect(const IntRect &value)
{
	IntRect &current = get();

	SDL_Rect r1 = { current.x, current.y, current.w, current.h };
	SDL_Rect r2 = { value.x,   value.y,   value.w,   value.h };

	SDL_Rect result;
	if (!SDL_IntersectRect(&r1, &r2, &result))
		result.w = result.h = 0;

	set(IntRect(result.x, result.y, result.w, result.h));
}

void GLScissorTest::apply(const bool &value)
{
	value ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
}

void GLTexture2D::apply(const bool &value)
{
	value ? glEnable(GL_TEXTURE_2D) : glDisable(GL_TEXTURE_2D);
}

void GLBlendMode::apply(const BlendType &value)
{
	switch (value)
	{
	case BlendNone :
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ZERO);
		break;

	case BlendNormal :
		glBlendEquation(GL_FUNC_ADD);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
		                    GL_ONE,       GL_ONE_MINUS_SRC_ALPHA);
		break;

	case BlendAddition :
		glBlendEquation(GL_FUNC_ADD);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE,
		                    GL_ONE,       GL_ONE);
		break;

	case BlendSubstraction :
		// FIXME Alpha calculation is untested
		glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE,
		                    GL_ONE,       GL_ONE);
		break;
	}
}

void GLViewport::apply(const IntRect &value)
{
	glViewport(value.x, value.y, value.w, value.h);
}


void GLState::setViewport(int width, int height)
{
	viewport.set(IntRect(0, 0, width, height));

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, width, 0, height, 0, 1);
	glMatrixMode(GL_MODELVIEW);
}

void GLState::pushSetViewport(int width, int height)
{
	viewport.pushSet(IntRect(0, 0, width, height));

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, width, 0, height, 0, 1);
	glMatrixMode(GL_MODELVIEW);
}

void GLState::popViewport()
{
	viewport.pop();

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
}

GLState::Caps::Caps()
{
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
}

GLState::GLState()
{
	clearColor.init(Vec4(0, 0, 0, 1));
	blendMode.init(BlendNormal);
	scissorTest.init(false);
	scissorBox.init(IntRect(0, 0, 640, 480));
	texture2D.init(true);
}