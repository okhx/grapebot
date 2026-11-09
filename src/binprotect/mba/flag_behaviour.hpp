#pragma once
#include <binwrite/binary/binary.hpp>

namespace binprotect::mba
{
	struct flag_dependant_t
	{
		std::uint32_t dependant_index;
		std::uint32_t closest_writer_index;
	};

	std::vector<binwrite::instruction_t> emulate_flag_behaviour(const binwrite::disassembled_instruction_t& instruction, const binwrite::encoder_operand_t& result, const binwrite::register_family_t& x_copy_register_family, const binwrite::encoder_operand_t& x_copy_register, const binwrite::encoder_operand_t& y, const binwrite::register_family_t& unused_register_family, const binwrite::encoder_operand_t& unused_register);

	std::deque<flag_dependant_t> find_flag_dependent_instructions(std::span<const binwrite::instruction_t> instructions);
	bool should_instruction_emulate_flags(std::deque<flag_dependant_t>& flag_dependants, std::uint32_t i, std::vector<binwrite::instruction_t>& obfuscated_instructions);
}
