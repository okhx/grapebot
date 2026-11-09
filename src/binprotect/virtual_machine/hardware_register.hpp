#pragma once
#include <binwrite/disassembler/disassembler.hpp>
#include <memory>

class vm_context_t;

class hardware_register_t
{
public:
	using value_type = binwrite::register_family_t;
	using size_type = std::uint16_t;

	hardware_register_t() = default;

	explicit hardware_register_t(std::shared_ptr<vm_context_t> vm_context, const value_type value)
				:	vm_context_(std::move(vm_context)),
					value_(value) { }

	~hardware_register_t();

	hardware_register_t(hardware_register_t&) = delete;
	hardware_register_t& operator=(hardware_register_t&) = delete;

	hardware_register_t(hardware_register_t&&) = default;
	hardware_register_t& operator=(hardware_register_t&&) = default;

	void free_self();

	[[nodiscard]] binwrite::register_t of_size(size_type size) const;
	[[nodiscard]] binwrite::register_t of_size(const binwrite::decoded_operand_t& operand) const;

	[[nodiscard]] value_type value() const
	{
		return value_;
	}

	[[nodiscard]] const value_type* operator->() const
	{
		return &value_;
	}

protected:
	std::shared_ptr<vm_context_t> vm_context_;
	value_type value_;
};