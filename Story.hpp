#pragma once

#include <cstdint>
#include <string>
#include <vector>

//The choice graph, loaded from the file that pack-story writes.
//Prose lives in one character blob and nodes point at spans of it, so the runtime never parses text.
struct Story {
	Story(std::string const &filename);

	struct Choice {
		uint32_t text_begin = 0;
		uint32_t text_end = 0;
		uint32_t target = 0;
	};
	static_assert(sizeof(Choice) == 12, "Story::Choice is packed");

	struct Node {
		uint32_t text_begin = 0;
		uint32_t text_end = 0;
		uint32_t choice_begin = 0;
		uint32_t choice_end = 0;
	};
	static_assert(sizeof(Node) == 16, "Story::Node is packed");

	std::string span(uint32_t begin, uint32_t end) const;

	std::vector< char > chars;
	std::vector< Node > nodes;
	std::vector< Choice > choices;
};
