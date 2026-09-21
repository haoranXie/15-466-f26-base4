#include "TextRenderer.hpp"

#include "ColorTextureProgram.hpp"
#include "gl_errors.hpp"

#include <hb-ft.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace {
	//the atlas is square and fixed. A face at reading size fills a small corner of it.
	constexpr uint32_t AtlasSize = 1024;

	//harfbuzz positions and freetype metrics both arrive in 26.6 fixed point
	constexpr float FixedScale = 1.0f / 64.0f;
} //anonymous namespace

//based on https://en.wikipedia.org/wiki/SRGB
glm::vec4 srgb_to_linear(glm::u8vec4 const &color) {
	return glm::vec4(
		std::pow(color.r / 255.0f, 2.2f),
		std::pow(color.g / 255.0f, 2.2f),
		std::pow(color.b / 255.0f, 2.2f),
		color.a / 255.0f
	);
}

//library, face and size setup are based on https://www.freetype.org/freetype2/docs/tutorial/step1.html
//the vertex buffer and attribute setup follow the pattern the base4 code uses for its own shader programs.
TextRenderer::TextRenderer(std::string const &font_filename, uint32_t pixel_size) {
	if (FT_Init_FreeType(&library)) {
		throw std::runtime_error("Failed to initialize freetype.");
	}
	if (FT_New_Face(library, font_filename.c_str(), 0, &face)) {
		throw std::runtime_error("Failed to load font '" + font_filename + "'.");
	}
	if (FT_Set_Pixel_Sizes(face, 0, pixel_size)) {
		throw std::runtime_error("Failed to set pixel size on font '" + font_filename + "'.");
	}

	line_height_pixels = float(face->size->metrics.height) * FixedScale;
	ascender_pixels = float(face->size->metrics.ascender) * FixedScale;

	//the same face backs both the atlas and the shaper, so glyph indices agree
	//based on https://github.com/harfbuzz/harfbuzz-tutorial/blob/master/hello-harfbuzz-freetype.c
	font = hb_ft_font_create_referenced(face);
	if (font == nullptr) {
		throw std::runtime_error("Failed to create a harfbuzz font from '" + font_filename + "'.");
	}

	{ //make the atlas texture, cleared to zero coverage
		std::vector< uint8_t > empty(size_t(AtlasSize) * size_t(AtlasSize), 0);
		glGenTextures(1, &atlas);
		glBindTexture(GL_TEXTURE_2D, atlas);
		//single-channel rows are not padded to four bytes, and the default alignment would shear them
		//based on https://www.khronos.org/opengl/wiki/Pixel_Transfer#Pixel_layout
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, AtlasSize, AtlasSize, 0, GL_RED, GL_UNSIGNED_BYTE, empty.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		//the atlas stores coverage in one channel, so swizzle it into alpha and the shader tints it
		//based on https://www.khronos.org/opengl/wiki/Texture#Swizzle_mask
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	{ //one buffer holds every glyph quad drawn in a frame
		glGenBuffers(1, &vertex_buffer);
		glGenVertexArrays(1, &vertex_array);
		glBindVertexArray(vertex_array);
		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
		glVertexAttribPointer(color_texture_program->Position_vec4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, position));
		glEnableVertexAttribArray(color_texture_program->Position_vec4);
		glVertexAttribPointer(color_texture_program->Color_vec4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, color));
		glEnableVertexAttribArray(color_texture_program->Color_vec4);
		glVertexAttribPointer(color_texture_program->TexCoord_vec2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, tex_coord));
		glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}

	GL_ERRORS();
}

//teardown of the face and the library is based on https://www.freetype.org/freetype2/docs/tutorial/step1.html
//and of the harfbuzz font on https://github.com/harfbuzz/harfbuzz-tutorial/blob/master/hello-harfbuzz-freetype.c
TextRenderer::~TextRenderer() {
	if (font) hb_font_destroy(font);
	if (face) FT_Done_Face(face);
	if (library) FT_Done_FreeType(library);
	if (vertex_array) glDeleteVertexArrays(1, &vertex_array);
	if (vertex_buffer) glDeleteBuffers(1, &vertex_buffer);
	if (atlas) glDeleteTextures(1, &atlas);
}

//glyph loading and rendering here are based on https://www.freetype.org/freetype2/docs/tutorial/step1.html
TextRenderer::Glyph const &TextRenderer::atlas_glyph(uint32_t index) {
	auto found = glyphs.find(index);
	if (found != glyphs.end()) return found->second;

	//harfbuzz reports glyph indices, so load by index and never by character
	if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT)) {
		throw std::runtime_error("Failed to load glyph index " + std::to_string(index) + ".");
	}
	if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
		throw std::runtime_error("Failed to render glyph index " + std::to_string(index) + ".");
	}

	FT_GlyphSlot slot = face->glyph;
	uint32_t width = slot->bitmap.width;
	uint32_t rows = slot->bitmap.rows;

	Glyph glyph;
	glyph.bearing = glm::vec2(float(slot->bitmap_left), float(slot->bitmap_top));
	glyph.size = glm::vec2(float(width), float(rows));

	if (width > 0 && rows > 0) {
		if (shelf_x + width > AtlasSize) { //start a new shelf when this one runs out of room
			shelf_x = 0;
			shelf_y += shelf_height;
			shelf_height = 0;
		}
		if (shelf_y + rows > AtlasSize) {
			throw std::runtime_error("Glyph atlas is full.");
		}

		//bitmap.pitch is the distance between rows and is not always the row width
		std::vector< uint8_t > pixels(size_t(width) * size_t(rows));
		for (uint32_t y = 0; y < rows; ++y) {
			uint8_t const *src = slot->bitmap.buffer + ptrdiff_t(y) * slot->bitmap.pitch;
			std::copy(src, src + width, pixels.begin() + ptrdiff_t(size_t(y) * size_t(width)));
		}

		glBindTexture(GL_TEXTURE_2D, atlas);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, GLint(shelf_x), GLint(shelf_y), GLsizei(width), GLsizei(rows), GL_RED, GL_UNSIGNED_BYTE, pixels.data());
		glBindTexture(GL_TEXTURE_2D, 0);

		glyph.uv_min = glm::vec2(float(shelf_x) / float(AtlasSize), float(shelf_y) / float(AtlasSize));
		glyph.uv_max = glm::vec2(float(shelf_x + width) / float(AtlasSize), float(shelf_y + rows) / float(AtlasSize));

		shelf_x += width;
		shelf_height = std::max(shelf_height, rows);
	}

	GL_ERRORS();
	return glyphs.emplace(index, glyph).first->second;
}

//shaping below is based on https://github.com/harfbuzz/harfbuzz-tutorial/blob/master/hello-harfbuzz-freetype.c
std::vector< TextRenderer::Run > TextRenderer::shape_run(std::string const &text) {
	hb_buffer_t *buffer = hb_buffer_create();
	hb_buffer_add_utf8(buffer, text.c_str(), int(text.size()), 0, int(text.size()));
	//set these explicitly so harfbuzz does not have to guess them from the string
	hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
	hb_buffer_set_script(buffer, HB_SCRIPT_LATIN);
	hb_buffer_set_language(buffer, hb_language_from_string("en", -1));

	hb_shape(font, buffer, nullptr, 0);

	unsigned int count = 0;
	hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &count);
	hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &count);

	std::vector< Run > runs(count);
	for (unsigned int i = 0; i < count; ++i) {
		//after shaping the codepoint field holds a glyph index
		runs[i].index = infos[i].codepoint;
		runs[i].cluster = infos[i].cluster;
		runs[i].offset = glm::vec2(float(positions[i].x_offset), float(positions[i].y_offset)) * FixedScale;
		runs[i].advance = float(positions[i].x_advance) * FixedScale;
	}

	hb_buffer_destroy(buffer);
	return runs;
}

TextRenderer::ShapedText TextRenderer::shape(std::string const &text, float wrap_width) {
	//authored newlines are hard breaks, so split on them before measuring anything
	std::vector< std::string > paragraphs;
	{
		size_t start = 0;
		while (true) {
			size_t end = text.find('\n', start);
			if (end == std::string::npos) {
				paragraphs.emplace_back(text.substr(start));
				break;
			}
			paragraphs.emplace_back(text.substr(start, end - start));
			start = end + 1;
		}
	}

	std::vector< std::string > lines;
	for (auto const &paragraph : paragraphs) {
		if (paragraph.empty()) {
			lines.emplace_back();
			continue;
		}

		std::vector< Run > runs = shape_run(paragraph);
		if (runs.empty()) {
			lines.emplace_back();
			continue;
		}

		//cumulative[i] is the pen distance to the start of run i, which makes any span a subtraction
		std::vector< float > cumulative(runs.size() + 1, 0.0f);
		for (size_t i = 0; i < runs.size(); ++i) cumulative[i + 1] = cumulative[i] + runs[i].advance;

		size_t line_start = 0;
		size_t candidate = std::string::npos; //run holding the last space that still fit
		for (size_t i = 0; i < runs.size(); ++i) {
			if (paragraph[runs[i].cluster] == ' ' && cumulative[i] - cumulative[line_start] <= wrap_width) {
				candidate = i;
			}
			if (cumulative[i + 1] - cumulative[line_start] > wrap_width && candidate != std::string::npos && candidate > line_start) {
				size_t from = runs[line_start].cluster;
				lines.emplace_back(paragraph.substr(from, runs[candidate].cluster - from));
				line_start = candidate + 1;
				candidate = std::string::npos;
			}
		}
		if (line_start < runs.size()) {
			lines.emplace_back(paragraph.substr(runs[line_start].cluster));
		}
	}

	//shape each line on its own so the first glyph of a line is shaped as a first glyph
	ShapedText shaped;
	float y = 0.0f;
	for (auto const &line : lines) {
		float x = 0.0f;
		for (auto const &run : shape_run(line)) {
			//harfbuzz y offsets point up and this layout grows down
			shaped.glyphs.emplace_back(PlacedGlyph{ run.index, glm::vec2(x + run.offset.x, y - run.offset.y) });
			x += run.advance;
		}
		shaped.size.x = std::max(shaped.size.x, x);
		y += line_height_pixels;
	}
	shaped.size.y = y;
	return shaped;
}

void TextRenderer::draw(ShapedText const &shaped, glm::vec2 const &anchor, glm::u8vec4 const &color) {
	glm::vec4 tint = srgb_to_linear(color);

	for (auto const &placed : shaped.glyphs) {
		Glyph const &glyph = atlas_glyph(placed.index);
		if (glyph.size.x == 0.0f || glyph.size.y == 0.0f) continue; //spaces carry an advance and no bitmap

		//round the pen to whole pixels so hinted glyphs land on the grid they were hinted for
		glm::vec2 pen = glm::vec2(std::round(anchor.x + placed.offset.x), std::round(anchor.y + placed.offset.y));
		//bitmap_top measures up from the baseline while this layout measures down from the window top
		float left = pen.x + glyph.bearing.x;
		float top = pen.y - glyph.bearing.y;
		float right = left + glyph.size.x;
		float bottom = top + glyph.size.y;

		Vertex a{ glm::vec2(left, top), tint, glm::vec2(glyph.uv_min.x, glyph.uv_min.y) };
		Vertex b{ glm::vec2(right, top), tint, glm::vec2(glyph.uv_max.x, glyph.uv_min.y) };
		Vertex c{ glm::vec2(right, bottom), tint, glm::vec2(glyph.uv_max.x, glyph.uv_max.y) };
		Vertex d{ glm::vec2(left, bottom), tint, glm::vec2(glyph.uv_min.x, glyph.uv_max.y) };

		vertices.emplace_back(a);
		vertices.emplace_back(b);
		vertices.emplace_back(c);
		vertices.emplace_back(a);
		vertices.emplace_back(c);
		vertices.emplace_back(d);
	}
}

void TextRenderer::flush(glm::uvec2 const &drawable_size) {
	if (vertices.empty()) return;

	//maps pixels with y down from the top left corner onto clip space
	glm::mat4 to_clip = glm::mat4(
		2.0f / float(drawable_size.x), 0.0f, 0.0f, 0.0f,
		0.0f, -2.0f / float(drawable_size.y), 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		-1.0f, 1.0f, 0.0f, 1.0f
	);

	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STREAM_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(color_texture_program->program);
	glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(to_clip));
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, atlas);
	glBindVertexArray(vertex_array);

	glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size()));

	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);

	vertices.clear();
	GL_ERRORS();
}
