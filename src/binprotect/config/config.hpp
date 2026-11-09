#pragma once
#include <optional>
#include <string>
#include <cstdint>

namespace binprotect::config
{
	struct obfuscation_t
	{
		using bool_type = std::uint8_t;

		std::string input_binary_file_path;
		std::string output_binary_file_path;
		std::string symbol_file_path;

		bool_type control_flow_flattening = true;
		bool_type virtual_machine = true;
		bool_type opaque_predicates = true;
		bool_type linear_substitution = true;
		std::uint8_t mixed_boolean_arithmetic_count = 2;
	};

	std::optional<obfuscation_t> parse(std::int32_t argc, const char** argv);
}
