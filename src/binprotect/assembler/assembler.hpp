#pragma once
#include <binwrite/assembler/assembler.hpp>
#include <binwrite/arch/mnemonic/mnemonic.hpp>

inline std::optional<binwrite::instruction_t> compile_assembler_instruction(const binwrite::mnemonic_t mnemonic, const std::span<const binwrite::encoder_operand_t> operands)
{
	const auto assembler_instruction = make_assembler_instruction(mnemonic, operands);

	if (!assembler_instruction)
	{
		return std::nullopt;
	}

	return assembler_instruction->compile();
}

inline std::optional<binwrite::instruction_t> generic_src_dest_instruction(const binwrite::mnemonic_t mnemonic, const binwrite::encoder_operand_t& source, const binwrite::encoder_operand_t& destination)
{
	std::array operands = { destination, source };

	return compile_assembler_instruction(mnemonic, operands);
}

inline std::optional<binwrite::instruction_t> generic_src_instruction(const binwrite::mnemonic_t mnemonic, const binwrite::encoder_operand_t& source)
{
	std::array operand = { source };

	return compile_assembler_instruction(mnemonic, operand);
}

inline std::optional<binwrite::instruction_t> generic_no_operand_instruction(const binwrite::mnemonic_t mnemonic)
{
	return compile_assembler_instruction(mnemonic, { });
}

inline std::optional<binwrite::instruction_t> pushfq_instruction()
{
	return generic_no_operand_instruction(binwrite::mnemonic_t::pushfq);
}

inline std::optional<binwrite::instruction_t> popfq_instruction()
{
	return generic_no_operand_instruction(binwrite::mnemonic_t::popfq);
}

inline std::optional<binwrite::instruction_t> nop_instruction()
{
	return generic_no_operand_instruction(binwrite::mnemonic_t::nop);
}

inline std::optional<binwrite::instruction_t> ret_instruction()
{
	return generic_no_operand_instruction(binwrite::mnemonic_t::ret);
}

inline binwrite::instruction_t int3_instruction()
{
	constexpr std::array bytes = { static_cast<std::uint8_t>(0xCC) };

	return binwrite::instruction_t{ bytes };
}

inline std::optional<binwrite::instruction_t> setnbe_instruction(const binwrite::encoder_operand_t& destination)
{
	return generic_src_instruction(binwrite::mnemonic_t::setnbe, destination);
}

inline std::optional<binwrite::instruction_t> setb_instruction(const binwrite::encoder_operand_t& destination)
{
	return generic_src_instruction(binwrite::mnemonic_t::setb, destination);
}

inline std::optional<binwrite::instruction_t> sets_instruction(const binwrite::encoder_operand_t& destination)
{
	return generic_src_instruction(binwrite::mnemonic_t::sets, destination);
}

inline std::optional<binwrite::instruction_t> push_instruction(const binwrite::encoder_operand_t& source)
{
	return generic_src_instruction(binwrite::mnemonic_t::push, source);
}

inline std::optional<binwrite::instruction_t> pop_instruction(const binwrite::encoder_operand_t& source)
{
	return generic_src_instruction(binwrite::mnemonic_t::pop, source);
}

inline binwrite::encoder_operand_t encode_unsigned_imm_operand(const std::uint64_t imm)
{
	binwrite::encoder_operand_t operand = { };

	operand.set_imm({ .u = imm });

	return operand;
}

inline binwrite::encoder_operand_t encode_signed_imm_operand(const std::int64_t imm)
{
	binwrite::encoder_operand_t operand = { };

	operand.set_imm({ .s = imm });

	return operand;
}

inline binwrite::encoder_operand_t encode_mem_operand(const binwrite::register_t base, const std::int64_t displacement, const std::uint16_t size, const binwrite::register_t index = binwrite::register_t::none, const std::uint8_t scale = 0)
{
	binwrite::encoder_operand_t operand = { };

	operand.set_mem({ .displacement = displacement, .scale = scale, .base = base, .index = index, .size = size });

	return operand;
}

static binwrite::encoder_operand_t encode_stack_mem_operand(const std::int64_t displacement, const std::uint16_t size)
{
	const binwrite::register_t stack_register = binwrite::register_t::rsp;

	return encode_mem_operand(stack_register, displacement, size);
}

inline std::optional<binwrite::instruction_t> jmp_instruction(const binwrite::encoder_operand_t& source)
{
	return generic_src_instruction(binwrite::mnemonic_t::jmp, source);
}

inline std::optional<binwrite::instruction_t> call_instruction(const binwrite::encoder_operand_t& source)
{
	return generic_src_instruction(binwrite::mnemonic_t::call, source);
}

inline std::optional<binwrite::instruction_t> jz_instruction(const binwrite::encoder_operand_t& source)
{
	return generic_src_instruction(binwrite::mnemonic_t::jz, source);
}

inline std::optional<binwrite::instruction_t> jnz_instruction(const binwrite::encoder_operand_t& source)
{
	return generic_src_instruction(binwrite::mnemonic_t::jnz, source);
}

inline std::optional<binwrite::instruction_t> jb_instruction(const binwrite::encoder_operand_t& source)
{
	return generic_src_instruction(binwrite::mnemonic_t::jb, source);
}

inline std::optional<binwrite::instruction_t> jnb_instruction(const binwrite::encoder_operand_t& source)
{
	return generic_src_instruction(binwrite::mnemonic_t::jnb, source);
}

inline std::optional<binwrite::instruction_t> cmp_instruction(const binwrite::encoder_operand_t& source, const binwrite::encoder_operand_t& destination)
{
	return generic_src_dest_instruction(binwrite::mnemonic_t::cmp, source, destination);
}

inline std::optional<binwrite::instruction_t> shl_instruction(const binwrite::encoder_operand_t& destination, const std::uint8_t shift_by)
{
	const binwrite::encoder_operand_t source = encode_unsigned_imm_operand(shift_by);

	return generic_src_dest_instruction(binwrite::mnemonic_t::shl, source, destination);
}

inline std::optional<binwrite::instruction_t> shr_instruction(const binwrite::encoder_operand_t& destination, const std::uint8_t shift_by)
{
	const binwrite::encoder_operand_t source = encode_unsigned_imm_operand(shift_by);

	return generic_src_dest_instruction(binwrite::mnemonic_t::shr, source, destination);
}

inline std::optional<binwrite::instruction_t> mov_instruction(const binwrite::encoder_operand_t& source, const binwrite::encoder_operand_t& destination)
{
	return generic_src_dest_instruction(binwrite::mnemonic_t::mov, source, destination);
}

inline std::optional<binwrite::instruction_t> lea_instruction(const binwrite::encoder_operand_t& source, const binwrite::encoder_operand_t& destination)
{
	return generic_src_dest_instruction(binwrite::mnemonic_t::lea, source, destination);
}

inline std::optional<binwrite::instruction_t> imul_instruction(const binwrite::encoder_operand_t& source, const binwrite::encoder_operand_t& destination)
{
	return generic_src_dest_instruction(binwrite::mnemonic_t::imul, source, destination);
}

inline std::optional<binwrite::instruction_t> test_instruction(const binwrite::encoder_operand_t& source, const binwrite::encoder_operand_t& destination)
{
	return generic_src_dest_instruction(binwrite::mnemonic_t::test, source, destination);
}

inline std::optional<binwrite::instruction_t> add_instruction(const binwrite::encoder_operand_t& source, const binwrite::encoder_operand_t& destination)
{
	return generic_src_dest_instruction(binwrite::mnemonic_t::add, source, destination);
}

inline std::optional<binwrite::instruction_t> neg_instruction(const binwrite::encoder_operand_t& destination)
{
	return generic_src_instruction(binwrite::mnemonic_t::neg, destination);
}

inline std::optional<binwrite::instruction_t> not_instruction(const binwrite::encoder_operand_t& destination)
{
	return generic_src_instruction(binwrite::mnemonic_t::not_, destination);
}

inline std::optional<binwrite::instruction_t> sub_instruction(const binwrite::encoder_operand_t& source, const binwrite::encoder_operand_t& destination)
{
	return generic_src_dest_instruction(binwrite::mnemonic_t::sub, source, destination);
}

inline std::optional<binwrite::instruction_t> and_instruction(const binwrite::encoder_operand_t& source, const binwrite::encoder_operand_t& destination)
{
	return generic_src_dest_instruction(binwrite::mnemonic_t::and_, source, destination);
}

inline std::optional<binwrite::instruction_t> or_instruction(const binwrite::encoder_operand_t& source, const binwrite::encoder_operand_t& destination)
{
	return generic_src_dest_instruction(binwrite::mnemonic_t::or_, source, destination);
}

inline std::optional<binwrite::instruction_t> xor_instruction(const binwrite::encoder_operand_t& source, const binwrite::encoder_operand_t& destination)
{
	return generic_src_dest_instruction(binwrite::mnemonic_t::xor_, source, destination);
}
