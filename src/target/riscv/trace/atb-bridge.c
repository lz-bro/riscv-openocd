// SPDX-License-Identifier: GPL-2.0-or-later
#include <time.h>

#include <helper/log.h>
#include "trace.h"
#include "trace_defines.h"
#include "target/target.h"
#include "../riscv.h"
#include "../field_helpers.h"

static int activate_atbbridge(struct target *target)
{
	RISCV_INFO(r);
	struct atbbridge_info *info = r->atbbridge_info;

	LOG_TARGET_DEBUG(target, "Activating the ATB Bridge with base address = 0x%" TARGET_PRIxADDR, info->base);

	uint32_t control;
	if (trace_reg_write(target, ATB_BRIDGE_CONTROL + info->base, ATB_BRIDGE_CONTROL_ACTIVE_ACTIVE) != ERROR_OK)
		return ERROR_FAIL;

	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for the ATB Bridge to become active.");
	while (1) {
		if (trace_reg_read(target, ATB_BRIDGE_CONTROL + info->base, &control) != ERROR_OK)
			return ERROR_FAIL;
		if (get_field32(control, ATB_BRIDGE_CONTROL_ACTIVE))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			LOG_TARGET_ERROR(target, "ATB Bridge (at address base=0x%" TARGET_PRIxADDR ") did not become active in %d s. "
					"Increase the timeout with 'riscv set_command_timeout_sec'.",
					info->base, riscv_get_command_timeout_sec());
			return ERROR_TIMEOUT_REACHED;
		}
	}
	LOG_TARGET_DEBUG(target, "ATB Bridge has become active.");

	uint32_t impl;
	if (trace_reg_read(target, ATB_BRIDGE_IMPL + info->base, &impl) != ERROR_OK)
		return ERROR_FAIL;

	if (get_field32(impl, ATB_BRIDGE_IMPL_VERMAJOR) != 0x1) {
		LOG_TARGET_ERROR(target, "the component is not compliant with version 1.0");
		return ERROR_FAIL;
	}

	if (get_field32(impl, ATB_BRIDGE_IMPL_COMPTYPE) != ATB_BRIDGE_IMPL_COMPTYPE_FLAG) {
		LOG_TARGET_ERROR(target, "ATB Bridge Component Type does not match expected value");
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

static int deactivate_atbbridge(struct target *target)
{
	RISCV_INFO(r);
	struct atbbridge_info *info = r->atbbridge_info;

	uint32_t control;
	if (trace_reg_read(target, ATB_BRIDGE_CONTROL + info->base, &control) != ERROR_OK)
		return ERROR_FAIL;
	if (get_field32(control, ATB_BRIDGE_CONTROL_ACTIVE)) {
		LOG_TARGET_DEBUG(target, "Initiating ATB Bridge reset.");
		if (trace_reg_write(target, ATB_BRIDGE_CONTROL + info->base, ATB_BRIDGE_CONTROL_ACTIVE_INACTIVE) != ERROR_OK)
			return ERROR_FAIL;
		const time_t start = time(NULL);
		LOG_TARGET_DEBUG(target, "Waiting for the ATB Bridge to become reset state.");
		while (1) {
			if (trace_reg_read(target, ATB_BRIDGE_CONTROL + info->base, &control) != ERROR_OK)
				return ERROR_FAIL;
			if (!get_field32(control, ATB_BRIDGE_CONTROL_ACTIVE))
				break;
			if (time(NULL) - start > riscv_get_command_timeout_sec()) {
				LOG_TARGET_ERROR(target, "ATB Bridge (at address base=0x%" TARGET_PRIxADDR ") did not become reset state in %d s. "
						"Increase the timeout with 'riscv set_command_timeout_sec'.",
						info->base, riscv_get_command_timeout_sec());
				return ERROR_TIMEOUT_REACHED;
			}
		}
	}
	LOG_TARGET_DEBUG(target, "ATB Bridge has become reset state.");
	return ERROR_OK;
}

static int reset_atbbridge(struct target *target)
{
	RISCV_INFO(r);
	struct atbbridge_info *info = r->atbbridge_info;

	if (deactivate_atbbridge(target) != ERROR_OK)
		return ERROR_FAIL;
	if (activate_atbbridge(target) != ERROR_OK)
		return ERROR_FAIL;
	info->was_reset = true;

	LOG_TARGET_DEBUG(target, "ATB Bridge successfully reset.");
	return ERROR_OK;
}

static int atbbridge_enable(struct target *target)
{
	RISCV_INFO(r);
	struct atbbridge_info *info = r->atbbridge_info;

	if (!info->was_reset && reset_atbbridge(target) != ERROR_OK)
		return ERROR_FAIL;

	uint32_t control = ATB_BRIDGE_CONTROL_ACTIVE | ATB_BRIDGE_CONTROL_ENABLE;
	control = set_field32(control, ATB_BRIDGE_CONTROL_BRIDCGEID, info->id);
	if (trace_reg_write(target, ATB_BRIDGE_CONTROL + info->base, control) != ERROR_OK)
		return ERROR_FAIL;

	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for the ATB Bridge to be enabled.");
	while (1) {
		if (trace_reg_read(target, ATB_BRIDGE_CONTROL + info->base, &control) != ERROR_OK)
			return ERROR_FAIL;
		if (get_field32(control, ATB_BRIDGE_CONTROL_ENABLE))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			LOG_TARGET_ERROR(target, "ATB Bridge (at address base=0x%" TARGET_PRIxADDR ") did not to be enabled in %d s. "
					"Increase the timeout with 'riscv set_command_timeout_sec'.",
					info->base, riscv_get_command_timeout_sec());
			return ERROR_TIMEOUT_REACHED;
		}
	}

	info->was_enabled = true;
	return ERROR_OK;
}

static int atbbridge_disable(struct target *target)
{
	RISCV_INFO(r);
	struct atbbridge_info *info = r->atbbridge_info;

	uint32_t control;
	if (trace_reg_read(target, ATB_BRIDGE_CONTROL + info->base, &control) != ERROR_OK)
		return ERROR_FAIL;
	control = set_field32(control, ATB_BRIDGE_CONTROL_ENABLE, ATB_BRIDGE_CONTROL_ENABLE_OFF);

	if (trace_reg_write(target, ATB_BRIDGE_CONTROL + info->base, control) != ERROR_OK)
		return ERROR_FAIL;

	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for the ATB Bridge to be disabled.");
	while (1) {
		if (trace_reg_read(target, ATB_BRIDGE_CONTROL + info->base, &control) != ERROR_OK)
			return ERROR_FAIL;
		if (!get_field32(control, ATB_BRIDGE_CONTROL_ENABLE))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			LOG_TARGET_ERROR(target, "ATB Bridge (at address base=0x%" TARGET_PRIxADDR ") did not to be disabled in %d s. "
					"Increase the timeout with 'riscv set_command_timeout_sec'.",
					info->base, riscv_get_command_timeout_sec());
			return ERROR_TIMEOUT_REACHED;
		}
	}

	info->was_enabled = false;
	return ERROR_OK;
}

enum atbbridge_cfg_opts {
	ATBBRIDGE_CFG_BASE,
	ATBBRIDGE_CFG_ID,
	ATBBRIDGE_CFG_INVALID = -1
};

static struct jim_nvp nvp_config_opts[] = {
	{ .name = "-base", .value = ATBBRIDGE_CFG_BASE },
	{ .name = "-id", .value = ATBBRIDGE_CFG_ID },
	{ .name = NULL, .value = ATBBRIDGE_CFG_INVALID }
};

COMMAND_HANDLER(handle_config_command)
{
	if (CMD_ARGC % 2) {
		LOG_ERROR("Command takes an even number of parameters.");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct atbbridge_info *info = r->atbbridge_info;

	if (CMD_ARGC == 0) {
		command_print(CMD, "ATB Bridge Base: 0x%" PRIx64, info->base);
		command_print(CMD, "ATB Bridge ID: 0x%x", info->id);
		return ERROR_OK;
	}

	struct jim_nvp *n;
	for (unsigned int i = 0; i < CMD_ARGC - 1; i += 2) {
		n = jim_nvp_name2value_simple(nvp_config_opts, CMD_ARGV[i]);
		switch (n->value) {
		case ATBBRIDGE_CFG_BASE:
			COMMAND_PARSE_ADDRESS(CMD_ARGV[i + 1], info->base);
			break;
		case ATBBRIDGE_CFG_ID:
			COMMAND_PARSE_NUMBER(u32, CMD_ARGV[i + 1], info->id);
			if (info->id == 0 || info->id >= 0x70) {
				LOG_ERROR("Values of 0x00 and 0x70-0x7F are reserved by the ATB specification and should not be used.");
				return ERROR_COMMAND_ARGUMENT_INVALID;
			}
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
	struct atbbridge_info *info = r->atbbridge_info;

       uint32_t impl, control;
       if (trace_reg_read(target, ATB_BRIDGE_IMPL + info->base, &impl) != ERROR_OK)
               return ERROR_FAIL;

       if (trace_reg_read(target, ATB_BRIDGE_CONTROL + info->base, &control) != ERROR_OK)
               return ERROR_FAIL;

	command_print(CMD, "atb-bridge @0x%" PRIx64, info->base);
	command_print(CMD, "    impl=0x%x", impl);
	command_print(CMD, "    control=0x%x", control);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_enable_command)
{
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct atbbridge_info *info = r->atbbridge_info;

	if (!info->was_enabled && atbbridge_enable(target) != ERROR_OK) {
		LOG_TARGET_ERROR(target, "Failed to enable ATB Bridge.");
		return ERROR_FAIL;
	}
	LOG_TARGET_INFO(target, "ATB Bridge successfully enable.");
	return ERROR_OK;
}

COMMAND_HANDLER(handle_disable_command)
{
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct atbbridge_info *info = r->atbbridge_info;

	if (!info->was_enabled) {
		LOG_TARGET_INFO(target, "ATB Bridge already disable.");
		return ERROR_OK;
	}

	if (atbbridge_disable(target) != ERROR_OK) {
		LOG_TARGET_ERROR(target, "Failed to disable ATB Bridge.");
		return ERROR_FAIL;
	}
	LOG_TARGET_INFO(target, "ATB Bridge successfully disable.");
	return ERROR_OK;
}

COMMAND_HANDLER(handle_close_command)
{
	struct target *target = get_current_target(CMD_CTX);

	return deactivate_atbbridge(target);
}

const struct command_registration atbbridge_command_handlers[] = {
	{
		.name = "config",
		.handler = handle_config_command,
		.mode = COMMAND_ANY,
		.help = "Configuration ATB bridge.",
		.usage = "atbbridge_attribute ...",
	},
	{
		.name = "info",
		.handler = handle_info_command,
		.mode = COMMAND_ANY,
		.help = "display ATB bridge info.",
		.usage = "",
	},
	{
		.name = "enable",
		.handler = handle_enable_command,
		.mode = COMMAND_EXEC,
		.help = "enable ATB bridge",
		.usage = "",
	},
	{
		.name = "disable",
		.handler = handle_disable_command,
		.mode = COMMAND_EXEC,
		.help = "disable ATB bridge",
		.usage = "",
	},
	{
		.name = "close",
		.handler = handle_close_command,
		.mode = COMMAND_EXEC,
		.help = "close ATB bridge hardware",
		.usage = "",
	},
	COMMAND_REGISTRATION_DONE
};

const struct command_registration atbbridge_command_group_handlers[] = {
       {
               .name = "atb-bridge",
               .mode = COMMAND_ANY,
               .help = "ATB Bridge command group",
               .usage = "",
               .chain = atbbridge_command_handlers,
       },
       COMMAND_REGISTRATION_DONE
};

