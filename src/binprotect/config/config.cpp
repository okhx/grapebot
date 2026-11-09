#include "config.hpp"
#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>

template <class T, class... Names>
static argparse::Argument& config_argument(argparse::ArgumentParser& parser, T& value,
	const std::string& help_text, Names&&... names)
{
	argparse::Argument& argument = parser.add_argument(std::forward<Names>(names)...)
		.help(help_text)
		.store_into(value);

	return argument;
}


template <class T, class... Names>
static argparse::Argument& config_argument_default(argparse::ArgumentParser& parser, T& value,
	const std::string& help_text, Names&&... names)
{
	const T& default_value = value;

	return config_argument(parser, value, help_text, std::forward<Names>(names)...)
		.default_value(default_value);
}


template <class... Names>
static argparse::Argument& uint8_config_argument(argparse::ArgumentParser& parser, std::uint8_t& value, const std::string& help_text, Names&&... names)
{
	return config_argument_default(parser, value, help_text, std::forward<Names>(names)...)
		.template scan<'i', std::uint8_t>();
}

template <class... Names>
static argparse::Argument& string_config_argument(argparse::ArgumentParser& parser, std::string& value, const std::string& help_text, Names&&... names)
{
	return config_argument(parser, value, help_text, std::forward<Names>(names)...);
}

std::optional<binprotect::config::obfuscation_t> binprotect::config::parse(const std::int32_t argc, const char** const argv)
{
	obfuscation_t config = { };

	argparse::ArgumentParser parser("binprotect");

	string_config_argument(parser, config.input_binary_file_path,
		"file path of input binary", "binary-path")
		.required();

	string_config_argument(parser, config.symbol_file_path,
		"file path of input binary's symbols", "symbol-path")
		.default_value(std::string{});

	string_config_argument(parser, config.output_binary_file_path,
		"desired file path of output binary", "--out", "--out-binary-path", "--out-path")
		.default_value(std::string{});

	uint8_config_argument(parser, config.control_flow_flattening, "enable control flow flattening pass", "--cff",
		"--control-flow-flattening");

	uint8_config_argument(parser, config.virtual_machine, "enable virtual machine pass", "--vm", "--virtual-machine");

	uint8_config_argument(parser, config.opaque_predicates, "enable opaque predicate pass", "--opa", "--opaque",
		"--opaque-predicate", "--opaque-predicates");

	uint8_config_argument(parser, config.linear_substitution, "enable linear substitution pass", "--lin",
		"--linear-substitution");

	uint8_config_argument(parser, config.mixed_boolean_arithmetic_count,
		"specify amount of mixed boolean arithmetic passes", "--mba",
		"--mixed-boolean-arithmetic");

	try
	{
		parser.parse_args(argc, argv);
	}
	catch (const std::exception& e)
	{
		spdlog::error("exception during argument parsing: '{}'", e.what());

		return std::nullopt;
	}

	return config;
}
