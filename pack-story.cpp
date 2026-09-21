//the chunk writing here uses read_write_chunk.hpp from the base4 code.
#include "Story.hpp"

#include "read_write_chunk.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

//Turns the authored story file into the chunk file the game loads.
//Run it by hand and commit the result. The build never calls it.
//
//The authored format is line based.
//  # a comment
//  :: node_name        starts a node
//  > label -> target   adds a choice to the node being read
//  anything else       is prose, and a blank line is a paragraph break

namespace {
	std::string trim(std::string const &text) {
		size_t begin = text.find_first_not_of(" \t\r");
		if (begin == std::string::npos) return "";
		size_t end = text.find_last_not_of(" \t\r");
		return text.substr(begin, end - begin + 1);
	}

	//a node as read from the source file, before names are turned into indices
	struct SourceNode {
		std::string name;
		std::string prose;
		bool blank_seen = false;
		std::vector< std::pair< std::string, std::string > > choices;
	};
} //anonymous namespace

static int pack(int argc, char **argv) {
	if (argc != 3) {
		std::cerr << "Usage: pack-story <story.txt> <story.chunk>" << std::endl;
		return 1;
	}
	std::string in_filename = argv[1];
	std::string out_filename = argv[2];

	std::ifstream in(in_filename);
	if (!in) {
		throw std::runtime_error("Failed to open '" + in_filename + "'.");
	}

	std::vector< SourceNode > source;
	std::unordered_map< std::string, uint32_t > by_name;

	std::string line;
	uint32_t line_number = 0;
	while (std::getline(in, line)) {
		line_number += 1;
		std::string text = trim(line);
		std::string where = "'" + in_filename + "' line " + std::to_string(line_number);

		if (!text.empty() && text[0] == '#') continue;

		if (text.rfind("::", 0) == 0) {
			std::string name = trim(text.substr(2));
			if (name.empty()) {
				throw std::runtime_error("Node with no name at " + where + ".");
			}
			if (by_name.count(name)) {
				throw std::runtime_error("Node '" + name + "' is defined twice, at " + where + ".");
			}
			by_name.emplace(name, uint32_t(source.size()));
			source.emplace_back();
			source.back().name = name;
			continue;
		}

		if (source.empty()) {
			if (text.empty()) continue;
			throw std::runtime_error("Text before the first node at " + where + ".");
		}

		if (!text.empty() && text[0] == '>') {
			size_t arrow = text.rfind("->");
			if (arrow == std::string::npos) {
				throw std::runtime_error("Choice with no target at " + where + ".");
			}
			std::string label = trim(text.substr(1, arrow - 1));
			std::string target = trim(text.substr(arrow + 2));
			if (label.empty() || target.empty()) {
				throw std::runtime_error("Choice with an empty label or target at " + where + ".");
			}
			source.back().choices.emplace_back(label, target);
			continue;
		}

		//wrapping in the source file is cosmetic, so only a blank line ends a paragraph
		if (text.empty()) {
			source.back().blank_seen = true;
			continue;
		}
		if (!source.back().prose.empty()) {
			source.back().prose += source.back().blank_seen ? "\n\n" : " ";
		}
		source.back().blank_seen = false;
		source.back().prose += text;
	}

	if (source.empty()) {
		throw std::runtime_error("'" + in_filename + "' contains no nodes.");
	}

	std::vector< char > chars;
	std::vector< Story::Node > nodes;
	std::vector< Story::Choice > choices;

	//append returns the span the text landed in. Repeated strings are stored twice because the file is small.
	auto append = [&chars](std::string const &text) {
		uint32_t begin = uint32_t(chars.size());
		chars.insert(chars.end(), text.begin(), text.end());
		return std::make_pair(begin, uint32_t(chars.size()));
	};

	for (auto const &node : source) {
		if (node.prose.empty()) {
			throw std::runtime_error("Node '" + node.name + "' in '" + in_filename + "' has no prose.");
		}

		Story::Node packed;
		auto prose_span = append(node.prose);
		packed.text_begin = prose_span.first;
		packed.text_end = prose_span.second;
		packed.choice_begin = uint32_t(choices.size());

		for (auto const &choice : node.choices) {
			auto found = by_name.find(choice.second);
			if (found == by_name.end()) {
				throw std::runtime_error("Node '" + node.name + "' in '" + in_filename + "' points at '" + choice.second + "', which does not exist.");
			}
			Story::Choice packed_choice;
			auto label_span = append(choice.first);
			packed_choice.text_begin = label_span.first;
			packed_choice.text_end = label_span.second;
			packed_choice.target = found->second;
			choices.emplace_back(packed_choice);
		}

		packed.choice_end = uint32_t(choices.size());
		nodes.emplace_back(packed);
	}

	std::ofstream out(out_filename, std::ios::binary);
	if (!out) {
		throw std::runtime_error("Failed to write '" + out_filename + "'.");
	}
	write_chunk("str0", chars, &out);
	write_chunk("node", nodes, &out);
	write_chunk("chce", choices, &out);

	std::cout << "Wrote " << nodes.size() << " nodes and " << choices.size() << " choices to '" << out_filename << "'." << std::endl;
	return 0;
}

//pack throws on bad data, and an exception leaving main dies without printing anything
int main(int argc, char **argv) {
	try {
		return pack(argc, argv);
	} catch (std::exception const &e) {
		std::cerr << "pack-story: " << e.what() << std::endl;
		return 1;
	}
}
