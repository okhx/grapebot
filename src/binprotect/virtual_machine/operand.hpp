#pragma once

#include "../assembler/assembler.hpp"
#include "hardware_register.hpp"
#include "binwrite/util/random.hpp"

class obfuscated_operand_t
{
public:
	using size_type = std::uint8_t;
	using value_type = std::uint8_t;

	explicit obfuscated_operand_t(const bool is_result)
			:	is_result_(is_result) { }

	virtual ~obfuscated_operand_t() = default;

	virtual binwrite::encoder_operand_t encode_imm(binwrite::encoder_operand_t::imm_t imm) = 0;

	virtual void encode_value(std::vector<binwrite::instruction_t>& instructions, const hardware_register_t& value_holder) = 0;
	virtual void decode_value(std::vector<binwrite::instruction_t>& instructions, const hardware_register_t& value_holder) = 0;

	[[nodiscard]] virtual bool can_evaluate_imm() const
	{
		return true;
	}

	[[nodiscard]] bool is_result() const
	{
		return is_result_;
	}

protected:
	bool is_result_;
};

class negated_operand_t final : public obfuscated_operand_t
{
public:
	explicit negated_operand_t(const bool is_result)
			:	obfuscated_operand_t(is_result) { }

	binwrite::encoder_operand_t encode_imm(const binwrite::encoder_operand_t::imm_t imm) override
	{
		return encode_unsigned_imm_operand(~imm.u);
	}

	void encode_value(std::vector<binwrite::instruction_t>& instructions, const hardware_register_t& value_holder) override
	{
		negate_value(instructions, value_holder);
	}

	void decode_value(std::vector<binwrite::instruction_t>& instructions, const hardware_register_t& value_holder) override
	{
		negate_value(instructions, value_holder);
	}

protected:
	static void negate_value(std::vector<binwrite::instruction_t>& instructions, const hardware_register_t& value_holder)
	{
		instructions.push_back(not_instruction(value_holder->qword).value());
	}
};

class xored_operand_t final : public obfuscated_operand_t
{
public:
	using key_type = std::uint16_t;

	explicit xored_operand_t(const bool is_result)
			:	obfuscated_operand_t(is_result),
				key_(binwrite::util::random_integral<key_type>()) { }

	binwrite::encoder_operand_t encode_imm(const binwrite::encoder_operand_t::imm_t imm) override
	{
		return encode_unsigned_imm_operand(imm.u ^ key_);
	}

	void encode_value(std::vector<binwrite::instruction_t>& instructions, const hardware_register_t& value_holder) override
	{
		xor_value(instructions, value_holder);
	}

	void decode_value(std::vector<binwrite::instruction_t>& instructions, const hardware_register_t& value_holder) override
	{
		xor_value(instructions, value_holder);
	}

protected:
	void xor_value(std::vector<binwrite::instruction_t>& instructions, const hardware_register_t& value_holder) const
	{
		const auto key_imm = encode_unsigned_imm_operand(key_);

		instructions.push_back(xor_instruction(key_imm, value_holder->qword).value());
	}

	key_type key_;
};

static std::unique_ptr<obfuscated_operand_t> random_obfuscated_operand(const bool is_result)
{
	if (binwrite::util::random_bool())
	{
		return std::make_unique<negated_operand_t>(is_result);
	}
	else
	{
		return std::make_unique<xored_operand_t>(is_result);
	}
}

static void encode_obfuscated_operand(std::vector<binwrite::instruction_t>& instructions,
                                      obfuscated_operand_t& obfuscated_operand,
                                      const binwrite::encoder_operand_t& redirected_operand,
                                      const binwrite::decoded_operand_t& original_operand,
                                      const hardware_register_t& holder)
{
	const std::uint16_t operand_width = original_operand.size();

	const auto sized_holder = original_operand.is_reg() || original_operand.is_imm()
		? holder->qword
		: holder->of_size(operand_width);

	if (redirected_operand.is_imm() && obfuscated_operand.can_evaluate_imm())
	{
		const auto imm = redirected_operand.imm();
		const auto encoded_imm = obfuscated_operand.encode_imm(imm);

		instructions.push_back(mov_instruction(encoded_imm, sized_holder).value());
	}
	else
	{
		instructions.push_back(mov_instruction(redirected_operand, sized_holder).value());
	
		obfuscated_operand.encode_value(instructions, holder);
	}
}

static std::unique_ptr<obfuscated_operand_t> obfuscate_operand(std::vector<binwrite::instruction_t>& instructions,
                                                               const binwrite::encoder_operand_t& redirected_operand,
                                                               const binwrite::decoded_operand_t& original_operand,
                                                               const hardware_register_t& holder)
{
	const bool is_result = original_operand.is_write_action();

	std::unique_ptr<obfuscated_operand_t> obfuscated_operand = random_obfuscated_operand(is_result);

	encode_obfuscated_operand(instructions, *obfuscated_operand, redirected_operand, original_operand, holder);

	return obfuscated_operand;
}
