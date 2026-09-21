#pragma once

#include "GL.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

//main.cpp turns on GL_FRAMEBUFFER_SRGB so colors have to be linear before they reach the shader
glm::vec4 srgb_to_linear(glm::u8vec4 const &color);

//Shapes utf-8 text with harfbuzz and rasterizes glyphs with freetype into one atlas texture.
//Glyphs are stored by glyph index, so the atlas ends up holding one copy of each glyph the face can
//draw, however much text the game has.
struct TextRenderer {
	TextRenderer(std::string const &font_filename, uint32_t pixel_size);
	~TextRenderer();

	//one glyph placed in a block of text, in pixels from the first baseline, y growing downward
	struct PlacedGlyph {
		uint32_t index = 0;
		glm::vec2 offset = glm::vec2(0.0f);
	};

	//text that has already been through harfbuzz. Hold one and reshape only when the string changes.
	struct ShapedText {
		std::vector< PlacedGlyph > glyphs;
		glm::vec2 size = glm::vec2(0.0f);
	};

	//breaks 'text' into lines no wider than 'wrap_width' pixels
	ShapedText shape(std::string const &text, float wrap_width);

	//adds quads to the pending batch. 'anchor' is the first baseline, measured down from the window top.
	void draw(ShapedText const &shaped, glm::vec2 const &anchor, glm::u8vec4 const &color);

	//draws everything added since the last flush in a single call
	void flush(glm::uvec2 const &drawable_size);

	float line_height() const { return line_height_pixels; }
	float ascender() const { return ascender_pixels; }

private:
	//where one glyph sits in the atlas, all in pixels
	struct Glyph {
		glm::vec2 bearing = glm::vec2(0.0f);
		glm::vec2 size = glm::vec2(0.0f);
		glm::vec2 uv_min = glm::vec2(0.0f);
		glm::vec2 uv_max = glm::vec2(0.0f);
	};

	//harfbuzz output for one line, already converted out of 26.6 fixed point
	struct Run {
		uint32_t index = 0;
		uint32_t cluster = 0;
		glm::vec2 offset = glm::vec2(0.0f);
		float advance = 0.0f;
	};

	std::vector< Run > shape_run(std::string const &text);
	Glyph const &atlas_glyph(uint32_t index);

	struct Vertex {
		glm::vec2 position;
		glm::vec4 color;
		glm::vec2 tex_coord;
	};
	static_assert(sizeof(Vertex) == 32, "TextRenderer vertex is packed");

	FT_Library library = nullptr;
	FT_Face face = nullptr;
	hb_font_t *font = nullptr;

	float line_height_pixels = 0.0f;
	float ascender_pixels = 0.0f;

	std::unordered_map< uint32_t, Glyph > glyphs;
	GLuint atlas = 0;
	//shelf packing state. 'shelf_y' is the top of the row being filled and 'shelf_x' the next free column.
	uint32_t shelf_x = 0;
	uint32_t shelf_y = 0;
	uint32_t shelf_height = 0;

	std::vector< Vertex > vertices;
	GLuint vertex_buffer = 0;
	GLuint vertex_array = 0;
};
