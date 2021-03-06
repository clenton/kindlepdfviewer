/*
    KindlePDFViewer: FreeType font rastering for UI
    Copyright (C) 2011 Hans-Werner Hilse <hilse@web.de>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <string.h>
//#include <stdio.h>
#include <stdint.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "blitbuffer.h"

/* for font access: */
#include <pdf/mupdf-internal.h>

#include "ft.h"

FT_Library freetypelib;

typedef struct KPVFace {
	FT_Face face;
	int allocated_face;
} KPVFace;

static int newFace(lua_State *L) {
	const char *filename = luaL_checkstring(L, 1);
	int pxsize = luaL_optint(L, 2, 16*64);

	KPVFace *face = (KPVFace*) lua_newuserdata(L, sizeof(KPVFace));
	luaL_getmetatable(L, "ft_face");
	lua_setmetatable(L, -2);

	face->allocated_face = 0;

	FT_Error error = FT_New_Face(freetypelib, filename, 0, &face->face);
	if(error) {
		return luaL_error(L, "freetype error");
	}

	face->allocated_face = 1;

	error = FT_Set_Pixel_Sizes(face->face, 0, pxsize);
	if(error) {
		error = FT_Done_Face(face->face);
		return luaL_error(L, "freetype error");
	}

	if(face->face->charmap == NULL) {
		//TODO
		//fprintf(stderr, "no unicode charmap found, to be implemented.\n");
	}
	return 1;
}

static int renderGlyph(lua_State *L) {
	KPVFace *face = (KPVFace*) luaL_checkudata(L, 1, "ft_face");
	int ch = luaL_checkint(L, 2);
	double bg = luaL_checknumber(L, 3);
	double fg = luaL_checknumber(L, 4);
	FT_Error error = FT_Load_Char(face->face, ch, FT_LOAD_RENDER);
	if(error) {
		return luaL_error(L, "freetype error");
	}

	int w = face->face->glyph->bitmap.width;
	int h = face->face->glyph->bitmap.rows;

	lua_newtable(L);

	BlitBuffer *bb;
	int result = newBlitBufferNative(L, w, h, 0, &bb);
	if(result != 1) {
		return result;
	}

	lua_setfield(L, -2, "bb");

	uint8_t *dst = bb->data;
	int y;
	int x;
	for(y = 0; y < h; y++) {
		uint8_t *src = face->face->glyph->bitmap.buffer + y * face->face->glyph->bitmap.pitch;
		for(x = 0; x < (w/2); x++) {
			*dst = (int)(0xFF * bg - src[0] * (bg - fg)) & 0xF0 |
				   (int)(0xFF * bg - src[1] * (bg - fg)) >> 4;
			src+=2;
			dst++;
		}
		if(w & 1) {
			*dst = (int)(0xFF * bg - *src * (bg - fg)) & 0xF0;
			dst++;
		}
	}

	lua_pushinteger(L, face->face->glyph->bitmap_left);
	lua_setfield(L, -2, "l");
	lua_pushinteger(L, face->face->glyph->bitmap_top);
	lua_setfield(L, -2, "t");
	lua_pushinteger(L, face->face->glyph->metrics.horiAdvance >> 6);
	lua_setfield(L, -2, "r");
	lua_pushinteger(L, face->face->glyph->advance.x >> 6);
	lua_setfield(L, -2, "ax");
	lua_pushinteger(L, face->face->glyph->advance.y >> 6);
	lua_setfield(L, -2, "ay");

	return 1;
}

static int hasKerning(lua_State *L) {
	KPVFace *face = (KPVFace*) luaL_checkudata(L, 1, "ft_face");
	if(FT_HAS_KERNING((face->face))) {
		lua_pushinteger(L, 1);
	} else {
		lua_pushinteger(L, 0);
	}
	return 1;
}

static int getKerning(lua_State *L) {
	KPVFace *face = (KPVFace*) luaL_checkudata(L, 1, "ft_face");
	int left = FT_Get_Char_Index(face->face, luaL_checkint(L, 2));
	int right = FT_Get_Char_Index(face->face, luaL_checkint(L, 3));
	FT_Vector kerning;
	FT_Error error = FT_Get_Kerning(face->face, left, right, FT_KERNING_DEFAULT, &kerning);
	if(error) {
		return luaL_error(L, "freetype error when getting kerning (l=%d, r=%d)", left, right);
	}
	lua_pushinteger(L, kerning.x >> 6);
	return 1;
}

static int getHeightAndAscender(lua_State *L) {
	KPVFace *face = (KPVFace*) luaL_checkudata(L, 1, "ft_face");

	double pixels_height,pixels_ascender;
	double em_size, y_scale;

	/* compute floating point scale factors */
	em_size = 1.0 * face->face->units_per_EM;
	y_scale = face->face->size->metrics.y_ppem / em_size;

	/* convert design distances to floating point pixels */
	pixels_height = face->face->height * y_scale;
	pixels_ascender = face->face->ascender * y_scale;

	lua_pushnumber(L, pixels_height);
	lua_pushnumber(L, pixels_ascender);
	return 2;
}

static int doneFace(lua_State *L) {
	KPVFace *face = (KPVFace*) luaL_checkudata(L, 1, "ft_face");
	if(face->allocated_face) {
		FT_Error error = FT_Done_Face(face->face);
		if(error) {
			return luaL_error(L, "freetype error when freeing face");
		}
	}
	return 0;
}

static const struct luaL_Reg ft_face_meth[] = {
	{"renderGlyph", renderGlyph},
	{"hasKerning", hasKerning},
	{"getKerning", getKerning},
	{"getHeightAndAscender", getHeightAndAscender},
	{"done", doneFace},
	{"__gc", doneFace},
	{NULL, NULL}
};

static const struct luaL_Reg ft_func[] = {
	{"newFace", newFace},
	{NULL, NULL}
};

int luaopen_ft(lua_State *L) {
	int error = FT_Init_FreeType(&freetypelib);
	if(error) {
		return luaL_error(L, "freetype error on initialization");
	}

	luaL_newmetatable(L, "ft_face");
	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);
	lua_settable(L, -3);
	luaL_register(L, NULL, ft_face_meth);

	luaL_register(L, "freetype", ft_func);
	return 1;
}
