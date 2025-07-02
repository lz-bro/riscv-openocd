// SPDX-License-Identifier: GPL-2.0-or-later
#include <time.h>

#include <helper/log.h>
#include "trace.h"
#include "trace_defines.h"
#include "target/target.h"
#include "../riscv.h"
#include "../field_helpers.h"

static int activate_funnel(struct target *target)
{
	RISCV_INFO(r);
	struct funnel_info *info = r->funnel_info;

	LOG_TARGET_DEBUG(target, "Activating the Trace Funnel with base address = 0x%" TARGET_PRIxADDR, info->base);

	uint32_t control;
	if (trace_reg_write(target, TF_CONTROL + info->base, TF_CONTROL_ACTIVE_ACTIVE) != ERROR_OK)
		return ERROR_FAIL;

	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for the Trace Funnel to become active.");
	while (1) {
		if (trace_reg_read(target, TF_CONTROL + info->base, &control) != ERROR_OK)
			return ERROR_FAIL;
		if (get_field32(control, TF_CONTROL_ACTIVE))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			LOG_TARGET_ERROR(target, "Trace Funnel (at address base=0x%" TARGET_PRIxADDR ") did not become active in %d s. "
					"Increase the timeout with 'riscv set_command_timeout_sec'.",
					info->base, riscv_get_command_timeout_sec());
			return ERROR_TIMEOUT_REACHED;
		}
	}
	LOG_TARGET_DEBUG(target, "Trace Funnel has become active.");

	uint32_t impl;
	if (trace_reg_read(target, TF_IMPL + info->base, &impl) != ERROR_OK)
		return ERROR_FAIL;

	if (get_field32(impl, TF_IMPL_VERMAJOR) != 0x1) {
		LOG_TARGET_ERROR(target, "the component is not compliant with version 1.0");
		return ERROR_FAIL;
	}

	if (get_field32(impl, TF_IMPL_COMPTYPE) != TF_IMPL_COMPTYPE_FLAG) {
		LOG_TARGET_ERROR(target, "Trace Funnel Component Type does not match expected value");
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

static int deactivate_funnel(struct target *target)
{
	RISCV_INFO(r);
	struct funnel_info *info = r->funnel_info;

	uint32_t control;
	if (trace_reg_read(target, TF_CONTROL + info->base, &control) != ERROR_OK)
		return ERROR_FAIL;
	if (get_field32(control, TF_CONTROL_ACTIVE)) {
		LOG_TARGET_DEBUG(target, "Initiating Trace Funnel reset.");
		if (trace_reg_write(target, TF_CONTROL + info->base, TF_CONTROL_ACTIVE_INACTIVE) != ERROR_OK)
			return ERROR_FAIL;
		const time_t start = time(NULL);
		LOG_TARGET_DEBUG(target, "Waiting for the Trace Funnel to become reset state.");
		while (1) {
			if (trace_reg_read(target, TF_CONTROL + info->base, &control) != ERROR_OK)
				return ERROR_FAIL;
			if (!get_field32(control, TF_CONTROL_ACTIVE))
				break;
			if (time(NULL) - start > riscv_get_command_timeout_sec()) {
				LOG_TARGET_ERROR(target, "Trace Funnel (at address base=0x%" TARGET_PRIxADDR ") did not become reset state in %d s. "
						"Increase the timeout with 'riscv set_command_timeout_sec'.",
						info->base, riscv_get_command_timeout_sec());
				return ERROR_TIMEOUT_REACHED;
			}
		}
	}
	LOG_TARGET_DEBUG(target, "Trace Funnel has become reset state.");
	return ERROR_OK;
}

static int reset_funnel(struct target *target)
{
	RISCV_INFO(r);
	struct funnel_info *info = r->funnel_info;

	if (deactivate_funnel(target) != ERROR_OK)
		return ERROR_FAIL;
	if (activate_funnel(target) != ERROR_OK)
		return ERROR_FAIL;
	info->was_reset = true;

	LOG_TARGET_DEBUG(target, "Trace Funnel successfully reset.");
	return ERROR_OK;
}

static int funnel_enable(struct target *target)
{
	RISCV_INFO(r);
	struct funnel_info *info = r->funnel_info;

	if (!info->was_reset && reset_funnel(target) != ERROR_OK)
		return ERROR_FAIL;

	uint32_t disinput;
	if (trace_reg_read(target, TF_DISINPUT + info->base, &disinput) != ERROR_OK)
		return ERROR_FAIL;
	disinput &= ~(info->port);
	if (trace_reg_write(target, TF_DISINPUT + info->base, disinput) != ERROR_OK)
		return ERROR_FAIL;

	uint32_t control = TF_CONTROL_ACTIVE | TF_CONTROL_ENABLE;
	if (trace_reg_write(target, TF_CONTROL + info->base, control) != ERROR_OK)
		return ERROR_FAIL;

	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for the Trace Funnel to be enabled.");
	while (1) {
		if (trace_reg_read(target, TF_CONTROL + info->base, &control) != ERROR_OK)
			return ERROR_FAIL;
		if (get_field32(control, TF_CONTROL_ENABLE))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			LOG_TARGET_ERROR(target, "Trace Funnel (at address base=0x%" TARGET_PRIxADDR ") did not to be enabled in %d s. "
					"Increase the timeout with 'riscv set_command_timeout_sec'.",
					info->base, riscv_get_command_timeout_sec());
			return ERROR_TIMEOUT_REACHED;
		}
	}

	info->was_enabled = true;
	return ERROR_OK;
}

static int funnel_disable(struct target *target)
{
	RISCV_INFO(r);
	struct funnel_info *info = r->funnel_info;

	uint32_t control;
	if (trace_reg_read(target, TE_CONTROL + info->base, &control) != ERROR_OK)
		return ERROR_FAIL;
	control = set_field32(control, TE_CONTROL_ENABLE, TE_CONTROL_ENABLE_OFF);

	if (trace_reg_write(target, TE_CONTROL + info->base, control) != ERROR_OK)
		return ERROR_FAIL;

	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for the Trace Funnel to be disabled.");
	while (1) {
		if (trace_reg_read(target, TE_CONTROL + info->base, &control) != ERROR_OK)
			return ERROR_FAIL;
		if (!get_field32(control, TE_CONTROL_ENABLE) && get_field32(control, TF_CONTROL_EMPTY))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			if (get_field32(control, TE_CONTROL_ENABLE))
				LOG_TARGET_ERROR(target, "Trace Funnel (at address base=0x%" TARGET_PRIxADDR ") did not to be disabled in %d s. "
						"Increase the timeout with 'riscv set_command_timeout_sec'.",
						info->base, riscv_get_command_timeout_sec());

			if (!get_field32(control, TF_CONTROL_EMPTY))
				LOG_TARGET_ERROR(target, "Trace Funnel (at address base=0x%" TARGET_PRIxADDR ") internal buffers did not become empty in %d s. "
						"Increase the timeout with 'riscv set_command_timeout_sec'.",
						info->base, riscv_get_command_timeout_sec());

			return ERROR_TIMEOUT_REACHED;
		}
	}

	if (trace_reg_write(target, TF_DISINPUT + info->base, TF_DISINPUT_MASK) != ERROR_OK)
		return ERROR_FAIL;

	info->was_enabled = false;
	return ERROR_OK;
}

enum funnel_cfg_opts {
	FUNNEL_CFG_BASE,
	FUNNEL_CFG_PORT,
	FUNNEL_CFG_INVALID = -1
};

static struct jim_nvp nvp_config_opts[] = {
	{ .name = "-base", .value = FUNNEL_CFG_BASE },
	{ .name = "-port", .value = FUNNEL_CFG_PORT },
	{ .name = NULL, .value = FUNNEL_CFG_INVALID }
};

COMMAND_HANDLER(handle_config_command)
{
	if (CMD_ARGC % 2) {
		LOG_ERROR("Command takes an even number of parameters.");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct funnel_info *info = r->funnel_info;

	if (CMD_ARGC == 0) {
		command_print(CMD, "Trace Funnel Base: 0x%" PRIx64, info->base);
		command_print(CMD, "Trace Funnel Port: 0x%x", info->port);
		return ERROR_OK;
	}

	struct jim_nvp *n;
	for (unsigned int i = 0; i < CMD_ARGC - 1; i += 2) {
		n = jim_nvp_name2value_simple(nvp_config_opts, CMD_ARGV[i]);
		switch (n->value) {
		case FUNNEL_CFG_BASE:
			COMMAND_PARSE_ADDRESS(CMD_ARGV[i + 1], info->base);
			break;
		case FUNNEL_CFG_PORT:
			COMMAND_PARSE_NUMBER(u32, CMD_ARGV[i + 1], info->port);
			break;
		default:
			return ERROR_COMMAND_SYNTAX_ERROR;
		}
	}
	return ERROR_OK;
}

COMMAND_HANDLER(handle_info_command)
{
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct funnel_info *info = r->funnel_info;

	uint32_t impl, control, disinput;
	if (trace_reg_read(target, TF_IMPL + info->base, &impl) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_read(target, TF_CONTROL + info->base, &control) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_read(target, TF_DISINPUT + info->base, &disinput) != ERROR_OK)
		return ERROR_FAIL;

	command_print(CMD, "trace-funner @0x%" PRIx64, info->base);
	command_print(CMD, "    impl=0x%x", impl);
	command_print(CMD, "    control=0x%x", control);
	command_print(CMD, "    disinput=0x%x", disinput);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_enable_command)
{
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct funnel_info *info = r->funnel_info;

	if (!info->was_enabled && funnel_enable(target) != ERROR_OK) {
		LOG_TARGET_ERROR(target, "Failed to enable Trace Funnel %d.", info->port);
		return ERROR_FAIL;
	}
	LOG_TARGET_INFO(target, "Trace Funnel %d enabled.", info->port);
	return ERROR_OK;
}

COMMAND_HANDLER(handle_disable_command)
{
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct funnel_info *info = r->funnel_info;

	if (!info->was_enabled) {
		LOG_TARGET_INFO(target, "Trace Funnel %d already disabled.", info->port);
		return ERROR_OK;
	}

	if (funnel_disable(target) != ERROR_OK) {
		LOG_TARGET_ERROR(target, "Failed to disable Trace Funnel %d.", info->port);
		return ERROR_FAIL;
	}
	LOG_TARGET_INFO(target, "Trace Funnel %d disabled.", info->port);
	return ERROR_OK;
}

COMMAND_HANDLER(handle_close_command)
{
	struct target *target = get_current_target(CMD_CTX);

	return deactivate_funnel(target);
}

const struct command_registration trace_funnel_command_handlers[] = {
	{
		.name = "config",
		.handler = handle_config_command,
		.mode = COMMAND_ANY,
		.help = "Configuration trace funnel.",
		.usage = "trace_funnel_attribute ...",
	},
	{
		.name = "info",
		.handler = handle_info_command,
		.mode = COMMAND_ANY,
		.help = "display trace funnel info.",
		.usage = "",
	},
	{
		.name = "enable",
		.handler = handle_enable_command,
		.mode = COMMAND_EXEC,
		.help = "enable trace funnel",
		.usage = "",
	},
	{
		.name = "disable",
		.handler = handle_disable_command,
		.mode = COMMAND_EXEC,
		.help = "disable trace funnel",
		.usage = "",
	},
	{
		.name = "close",
		.handler = handle_close_command,
		.mode = COMMAND_EXEC,
		.help = "close trace funnel hardware",
		.usage = "",
	},
	COMMAND_REGISTRATION_DONE
};

const struct command_registration trace_funnel_command_group_handlers[] = {
       {
               .name = "trace-funnel",
               .mode = COMMAND_ANY,
               .help = "Trace Funnel command group",
               .usage = "",
               .chain = trace_funnel_command_handlers,
       },
       COMMAND_REGISTRATION_DONE
};
