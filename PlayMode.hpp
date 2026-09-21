#pragma once

#include "Mode.hpp"

#include "Story.hpp"
#include "TextRenderer.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	uint32_t node = 0;
	uint32_t selected = 0;

	void go_to(uint32_t next_node);
	void choose(uint32_t choice);

	//----- shaped text -----

	//shaping happens on a node change or a resize, never per frame
	TextRenderer::ShapedText prose;
	std::vector< TextRenderer::ShapedText > options;
	glm::uvec2 shaped_for = glm::uvec2(0);
	bool needs_shape = true;

	void reshape(glm::uvec2 const &drawable_size);
	//picks the one glyph size that lets every node in the story fit this window
	void fit_pixel_size(glm::uvec2 const &drawable_size);
	float block_height(Story::Node const &story_node, float column);
	std::string option_label(Story::Node const &story_node, uint32_t choice) const;
};
