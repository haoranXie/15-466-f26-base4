//the chunk reading here uses read_write_chunk.hpp from the base4 code.
#include "Story.hpp"

#include "read_write_chunk.hpp"

#include <fstream>
#include <stdexcept>

Story::Story(std::string const &filename) {
	std::ifstream file(filename, std::ios::binary);
	if (!file) {
		throw std::runtime_error("Failed to open story file '" + filename + "'.");
	}

	read_chunk(file, "str0", &chars);
	read_chunk(file, "node", &nodes);
	read_chunk(file, "chce", &choices);

	if (nodes.empty()) {
		throw std::runtime_error("Story file '" + filename + "' has no nodes.");
	}

	//a bad index here means the packer wrote a broken file, so name the node that carries it
	for (uint32_t i = 0; i < nodes.size(); ++i) {
		Node const &node = nodes[i];
		if (node.text_end > chars.size() || node.text_begin > node.text_end) {
			throw std::runtime_error("Story file '" + filename + "' node " + std::to_string(i) + " has an out of range text span.");
		}
		if (node.choice_end > choices.size() || node.choice_begin > node.choice_end) {
			throw std::runtime_error("Story file '" + filename + "' node " + std::to_string(i) + " has an out of range choice span.");
		}
		for (uint32_t c = node.choice_begin; c < node.choice_end; ++c) {
			if (choices[c].target >= nodes.size()) {
				throw std::runtime_error("Story file '" + filename + "' node " + std::to_string(i) + " has a choice pointing outside the graph.");
			}
		}
	}
}

std::string Story::span(uint32_t begin, uint32_t end) const {
	return std::string(chars.begin() + begin, chars.begin() + end);
}
