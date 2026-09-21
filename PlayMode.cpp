//the Load<> blocks and the draw() scaffolding here are based on the PlayMode.cpp that ships with the base4 code.
#include "PlayMode.hpp"

#include "Load.hpp"
#include "data_path.hpp"
#include "gl_errors.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace {
	//about forty five lines of text fit the window height at this ratio
	constexpr float PixelSizeFromHeight = 1.0f / 22.5f;
	//the column is a multiple of the glyph size so a line holds the same number of words on any monitor
	constexpr float ColumnPerPixelSize = 26.0f;
	//fractions of the window height kept clear above and below the text
	constexpr float TopFraction = 0.09f;
	constexpr float BottomFraction = 0.06f;
	constexpr uint32_t MinPixelSize = 11;
	constexpr char RestartLabel[] = "1. Begin again";

	constexpr glm::u8vec4 Background = glm::u8vec4(0x0e, 0x0e, 0x0e, 0xff);
	constexpr glm::u8vec4 TextColor = glm::u8vec4(0xe4, 0xe0, 0xd8, 0xff);
	constexpr glm::u8vec4 DimColor = glm::u8vec4(0x66, 0x64, 0x60, 0xff);

	//a tall narrow window is limited by its width, so take the smaller of the two as the height to scale from
	uint32_t pixel_size_for(glm::uvec2 const &drawable_size) {
		float usable = std::min(float(drawable_size.y), float(drawable_size.x) * 9.0f / 16.0f);
		return std::max(MinPixelSize, uint32_t(std::round(usable * PixelSizeFromHeight)));
	}
} //anonymous namespace

//the renderer is written to while drawing, so it sits next to its loader the way the base code keeps its vao
static TextRenderer *text_renderer = nullptr;
static uint32_t text_pixel_size = 0;

static void set_pixel_size(uint32_t pixel_size) {
	if (pixel_size == text_pixel_size) return;
	//glyphs are hinted for one pixel size, so a new size needs a fresh atlas
	delete text_renderer;
	text_renderer = new TextRenderer(data_path("EBGaramond-Regular.ttf"), pixel_size);
	text_pixel_size = pixel_size;
}

static float column_for(glm::uvec2 const &drawable_size) {
	return std::min(float(drawable_size.x) * 0.76f, float(text_pixel_size) * ColumnPerPixelSize);
}

Load< void > load_text_renderer(LoadTagDefault, []() {
	set_pixel_size(32);
});

Load< Story > story(LoadTagDefault, []() -> Story const * {
	return new Story(data_path("story.chunk"));
});

PlayMode::PlayMode() {
	go_to(0);
}

PlayMode::~PlayMode() {
}

void PlayMode::go_to(uint32_t next_node) {
	if (next_node >= story->nodes.size()) {
		throw std::runtime_error("Tried to enter node " + std::to_string(next_node) + ", which is not in the story.");
	}
	node = next_node;
	selected = 0;
	needs_shape = true;
}

void PlayMode::choose(uint32_t choice) {
	Story::Node const &current_node = story->nodes[node];
	uint32_t count = current_node.choice_end - current_node.choice_begin;
	if (count == 0) {
		go_to(0);
		return;
	}
	if (choice >= count) return;
	go_to(story->choices[current_node.choice_begin + choice].target);
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	if (evt.type != SDL_EVENT_KEY_DOWN) return false;

	uint32_t shown = uint32_t(options.size());
	if (shown == 0) return false;

	if (evt.key.key == SDLK_RETURN || evt.key.key == SDLK_SPACE) {
		choose(selected);
		return true;
	}
	if (evt.key.key == SDLK_UP || evt.key.key == SDLK_W) {
		selected = (selected + shown - 1) % shown;
		return true;
	}
	if (evt.key.key == SDLK_DOWN || evt.key.key == SDLK_S) {
		selected = (selected + 1) % shown;
		return true;
	}
	if (evt.key.key >= SDLK_1 && evt.key.key <= SDLK_9) {
		uint32_t index = uint32_t(evt.key.key - SDLK_1);
		if (index < shown) choose(index);
		return true;
	}

	return false;
}

std::string PlayMode::option_label(Story::Node const &story_node, uint32_t choice) const {
	Story::Choice const &c = story->choices[choice];
	return std::to_string(choice - story_node.choice_begin + 1) + ". " + story->span(c.text_begin, c.text_end);
}

float PlayMode::block_height(Story::Node const &story_node, float column) {
	float height = text_renderer->shape(story->span(story_node.text_begin, story_node.text_end), column).size.y;
	height += text_renderer->line_height() * 0.5f;
	if (story_node.choice_begin == story_node.choice_end) {
		height += text_renderer->shape(RestartLabel, column).size.y;
	}
	for (uint32_t i = story_node.choice_begin; i < story_node.choice_end; ++i) {
		height += text_renderer->shape(option_label(story_node, i), column).size.y;
	}
	return height;
}

void PlayMode::fit_pixel_size(glm::uvec2 const &drawable_size) {
	float room = float(drawable_size.y) * (1.0f - TopFraction - BottomFraction);
	uint32_t pixel_size = pixel_size_for(drawable_size);

	//the whole story is measured so the text keeps one size from the first node to the last
	while (true) {
		set_pixel_size(pixel_size);
		float column = column_for(drawable_size);
		float tallest = 0.0f;
		for (auto const &story_node : story->nodes) {
			tallest = std::max(tallest, block_height(story_node, column));
		}
		if (tallest <= room || pixel_size <= MinPixelSize) break;
		pixel_size = std::max(MinPixelSize, uint32_t(float(pixel_size) * 0.85f));
	}
}

void PlayMode::reshape(glm::uvec2 const &drawable_size) {
	if (shaped_for != drawable_size) {
		fit_pixel_size(drawable_size);
		shaped_for = drawable_size;
	}

	float column = column_for(drawable_size);
	Story::Node const &current_node = story->nodes[node];
	prose = text_renderer->shape(story->span(current_node.text_begin, current_node.text_end), column);

	options.clear();
	for (uint32_t i = current_node.choice_begin; i < current_node.choice_end; ++i) {
		options.emplace_back(text_renderer->shape(option_label(current_node, i), column));
	}
	//an ending has nowhere to go, so it offers the one line that starts the story over
	if (options.empty()) {
		options.emplace_back(text_renderer->shape(RestartLabel, column));
	}

	needs_shape = false;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	if (needs_shape || shaped_for != drawable_size) reshape(drawable_size);

	glm::vec4 clear = srgb_to_linear(Background);
	glClearColor(clear.r, clear.g, clear.b, clear.a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	float column = column_for(drawable_size);
	float left = std::round((float(drawable_size.x) - column) * 0.5f);
	float line = text_renderer->line_height();

	//the first baseline sits one ascender below the top margin
	float y = std::round(float(drawable_size.y) * TopFraction) + text_renderer->ascender();
	text_renderer->draw(prose, glm::vec2(left, y), TextColor);

	y += prose.size.y + line * 0.5f;
	for (uint32_t i = 0; i < options.size(); ++i) {
		text_renderer->draw(options[i], glm::vec2(left, y), i == selected ? TextColor : DimColor);
		y += options[i].size.y;
	}

	//every glyph on screen goes out in one draw call
	text_renderer->flush(drawable_size);

	GL_ERRORS();
}
