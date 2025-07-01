// SPDX-License-Identifier: GPL-2.0-or-later
#include <time.h>

#include <helper/log.h>
#include "trace.h"
#include "trace_defines.h"
#include "target/target.h"
#include "../riscv.h"
#include "../field_helpers.h"

static int activate_timestamp(struct target *target)
{
	RISCV_INFO(r);
	struct timestamp_info *info = r->timestamp_info;

	LOG_TARGET_DEBUG(target, "Activating the Timestamp with base address = 0x%" TARGET_PRIxADDR, info->base);

	uint32_t control;
	if (trace_reg_write(target, TS_CONTROL + info->base, TS_CONTROL_ACTIVE_ACTIVE) != ERROR_OK)
		return ERROR_FAIL;

	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for the Timestamp to become active.");
	while (1) {
		if (trace_reg_read(target, TS_CONTROL + info->base, &control) != ERROR_OK)
			return ERROR_FAIL;
		if (get_field32(control, TS_CONTROL_ACTIVE))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			LOG_TARGET_ERROR(target, "Timestamp (at address base=0x%" TARGET_PRIxADDR ") did not become active in %d s. "
					"Increase the timeout with 'riscv set_command_timeout_sec'.",
					info->base, riscv_get_command_timeout_sec());
			return ERROR_TIMEOUT_REACHED;
		}
	}
	LOG_TARGET_DEBUG(target, "Timestamp has become active.");
	return ERROR_OK;
}

static int deactivate_timestamp(struct target *target)
{
	RISCV_INFO(r);
	struct timestamp_info *info = r->timestamp_info;

	uint32_t control;
	if (trace_reg_read(target, TS_CONTROL + info->base, &control) != ERROR_OK)
		return ERROR_FAIL;
	if (get_field32(control, TS_CONTROL_ACTIVE)) {
		LOG_TARGET_DEBUG(target, "Initiating Timestamp reset.");
		if (trace_reg_write(target, TS_CONTROL + info->base, TS_CONTROL_ACTIVE_INACTIVE) != ERROR_OK)
			return ERROR_FAIL;
		const time_t start = time(NULL);
		LOG_TARGET_DEBUG(target, "Waiting for the Timestamp to become reset state.");
		while (1) {
			if (trace_reg_read(target, TS_CONTROL + info->base, &control) != ERROR_OK)
				return ERROR_FAIL;
			if (!get_field32(control, TS_CONTROL_ACTIVE))
				break;
			if (time(NULL) - start > riscv_get_command_timeout_sec()) {
				LOG_TARGET_ERROR(target, "Timestamp (at address base=0x%" TARGET_PRIxADDR ") did not become reset state in %d s. "
						"Increase the timeout with 'riscv set_command_timeout_sec'.",
						info->base, riscv_get_command_timeout_sec());
				return ERROR_TIMEOUT_REACHED;
			}
		}
	}
	LOG_TARGET_DEBUG(target, "Timestamp has become reset state.");
	return ERROR_OK;
}

static int reset_timestamp(struct target *target)
{
	RISCV_INFO(r);
	struct timestamp_info *info = r->timestamp_info;

	if (!info->impl_reset) {
		uint32_t control;
		if (trace_reg_read(target, TS_CONTROL + info->base, &control) != ERROR_OK)
			return ERROR_FAIL;
		if (!get_field32(control, TS_CONTROL_ACTIVE)) {
			LOG_TARGET_ERROR(target, "Reset for timestamp is implemented, but did not become active. "
					"Please check the corresponding active for trace-encoder or trace-funnel.");
			return ERROR_FAIL;
		}
		info->was_reset = true;
		return ERROR_OK;
	}

	if (deactivate_timestamp(target) != ERROR_OK)
		return ERROR_FAIL;
		LOG_TARGET_INFO(target, "reset for timestamp component may not be implemented.");
	if (activate_timestamp(target) != ERROR_OK)
		return ERROR_FAIL;
	info->was_reset = true;

	LOG_TARGET_DEBUG(target, "Timestamp successfully reset.");
	return ERROR_OK;
}

static struct jim_nvp nvp_mode[] = {
	{ .name = "no", .value = NO_MODE },
	{ .name = "external", .value = EXTERNAL },
	{ .name = "system", .value = INTERNAL_SYSTEM },
	{ .name = "core", .value = INTERNAL_CORE },
	{ .name = "shared", .value = SHARED },
	{ .name = NULL, .value = -1 },
};

enum timestamp_cfg_opts {
	TIMESTAMP_CFG_BASE,
	TIMESTAMP_CFG_IMPL_RESET,
	TIMESTAMP_CFG_RUN_HALT,
	TIMESTAMP_CFG_MODE,
	TIMESTAMP_CFG_PRESCALE,
	TIMESTAMP_CFG_INVALID = -1
};

static struct jim_nvp nvp_config_opts[] = {
	{ .name = "-base", .value = TIMESTAMP_CFG_BASE },
	{ .name = "-impl_reset", .value = TIMESTAMP_CFG_IMPL_RESET },
	{ .name = "-run_halt", .value = TIMESTAMP_CFG_RUN_HALT },
	{ .name = "-mode", .value = TIMESTAMP_CFG_MODE },
	{ .name = "-prescale", .value = TIMESTAMP_CFG_PRESCALE },
	{ .name = NULL, .value = TIMESTAMP_CFG_INVALID }
};

COMMAND_HANDLER(handle_config_command)
{
	if (CMD_ARGC % 2) {
		LOG_ERROR("Command takes an even number of parameters.");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct timestamp_info *info = r->timestamp_info;

	if (CMD_ARGC == 0) {
		command_print(CMD, "Timestamp Base: 0x%" PRIx64, info->base);
		command_print(CMD, "Timestamp reset bit is implemented: %s", info->impl_reset ? "on" : "off");
		command_print(CMD, "Timestamp counter runs when hart is halted: %s", info->run_halt ? "on" : "off");
		command_print(CMD, "Timestamp Mode: %s", jim_nvp_value2name_simple(nvp_mode, info->mode)->name);
		command_print(CMD, "Timestamp Prescale: %d", info->prescale);
		return ERROR_OK;
	}

	struct jim_nvp *n;
	for (unsigned int i = 0; i < CMD_ARGC - 1; i += 2) {
		n = jim_nvp_name2value_simple(nvp_config_opts, CMD_ARGV[i]);
		switch (n->value) {
		case TIMESTAMP_CFG_BASE:
			COMMAND_PARSE_ADDRESS(CMD_ARGV[i + 1], info->base);
			break;
		case TIMESTAMP_CFG_IMPL_RESET:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->impl_reset);
			break;
		case TIMESTAMP_CFG_RUN_HALT:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->run_halt);
			break;
		case TIMESTAMP_CFG_MODE:
			if (!jim_nvp_name2value_simple(nvp_mode, CMD_ARGV[i + 1])->name)
				return ERROR_COMMAND_SYNTAX_ERROR;
			info->mode = jim_nvp_name2value_simple(nvp_mode, CMD_ARGV[i + 1])->value;
			break;
		case TIMESTAMP_CFG_PRESCALE:
			COMMAND_PARSE_NUMBER(u32, CMD_ARGV[i + 1], info->prescale);
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
	struct timestamp_info *info = r->timestamp_info;

	uint32_t control, counter_low, counter_high;
	if (trace_reg_read(target, TS_CONTROL + info->base, &control) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_read(target, TS_COUNTERLOW + info->base, &counter_low) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_read(target, TS_COUNTERHIGH + info->base, &counter_high) != ERROR_OK)
		return ERROR_FAIL;

	command_print(CMD, "timestamp @0x%" PRIx64, info->base);
	command_print(CMD, "    control: 0x%x", control);
	command_print(CMD, "    counter_low: 0x%x", counter_low);
	command_print(CMD, "    counter_high: 0x%x", counter_high);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_enable_command)
{
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct timestamp_info *info = r->timestamp_info;

	if (!info->was_reset && reset_timestamp(target) != ERROR_OK)
		return ERROR_FAIL;

	uint32_t control = TS_CONTROL_ACTIVE | TS_CONTROL_COUNT_RUNS | TS_CONTROL_RESET  | TS_CONTROL_ENABLE;
	control = set_field32(control, TS_CONTROL_RUNINDEBUG, info->run_halt);
	control = set_field32(control, TS_CONTROL_MODE, info->mode);
	control = set_field32(control, TS_CONTROL_PRESCALE, info->prescale);

	if (trace_reg_write(target, TS_CONTROL + info->base, control) != ERROR_OK)
		return ERROR_FAIL;

	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for the Timestamp to be enabled.");
	while (1) {
		if (trace_reg_read(target, TS_CONTROL + info->base, &control) != ERROR_OK)
			return ERROR_FAIL;
		if (get_field32(control, TS_CONTROL_ENABLE))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			LOG_TARGET_ERROR(target, "Timestamp (at address base=0x%" TARGET_PRIxADDR ") did not to be enabled in %d s. "
					"Increase the timeout with 'riscv set_command_timeout_sec'.",
					info->base, riscv_get_command_timeout_sec());
			return ERROR_TIMEOUT_REACHED;
		}
	}

	if (get_field32(control, TS_CONTROL_RUNINDEBUG) != info->run_halt) {
		LOG_TARGET_WARNING(target, "trTsRunInDebug %d is not supported.", info->run_halt);
		info->run_halt = get_field32(control, TS_CONTROL_RUNINDEBUG);
	}

	if (get_field32(control, TS_CONTROL_MODE) != info->mode) {
		LOG_TARGET_WARNING(target, "trTsMode %d is not supported.", info->mode);
		info->mode = get_field32(control, TS_CONTROL_MODE);
	}

	if (get_field32(control, TS_CONTROL_PRESCALE) != info->prescale) {
		LOG_TARGET_WARNING(target, "trTsPrescale %d is not supported.", info->prescale);
		info->prescale = get_field32(control, TS_CONTROL_PRESCALE);
	}

	info->was_enabled = true;

	LOG_TARGET_INFO(target, "Timestamp enabled.");
	return ERROR_OK;
}

COMMAND_HANDLER(handle_disable_command)
{
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct timestamp_info *info = r->timestamp_info;

	if (!info->was_enabled) {
		LOG_TARGET_INFO(target, "Timestamp already disable.");
		return ERROR_OK;
	}

	uint32_t control;
	if (trace_reg_read(target, TS_CONTROL + info->base, &control) != ERROR_OK)
		return ERROR_FAIL;
	control = set_field32(control, TS_CONTROL_ENABLE, TS_CONTROL_ENABLE_OFF);

	if (trace_reg_write(target, TS_CONTROL + info->base, control) != ERROR_OK)
		return ERROR_FAIL;

	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for the Timestamp to be disabled.");
	while (1) {
		if (trace_reg_read(target, TS_CONTROL + info->base, &control) != ERROR_OK)
			return ERROR_FAIL;
		if (!get_field32(control, TS_CONTROL_ENABLE))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			LOG_TARGET_ERROR(target, "Timestamp (at address base=0x%" TARGET_PRIxADDR ") did not to be disabled in %d s. "
					"Increase the timeout with 'riscv set_command_timeout_sec'.",
					info->base, riscv_get_command_timeout_sec());
			return ERROR_TIMEOUT_REACHED;
		}
	}

	info->was_enabled = false;
	return ERROR_OK;
}

COMMAND_HANDLER(handle_close_command)
{
	struct target *target = get_current_target(CMD_CTX);

	return deactivate_timestamp(target);
}

const struct command_registration timestamp_command_handlers[] = {
	{
		.name = "config",
		.handler = handle_config_command,
		.mode = COMMAND_ANY,
		.help = "Configuration timestamp.",
		.usage = "timestamp_attribute ...",
	},
	{
		.name = "info",
		.handler = handle_info_command,
		.mode = COMMAND_ANY,
		.help = "display timestamp info.",
		.usage = "",
	},
	{
		.name = "enable",
		.handler = handle_enable_command,
		.mode = COMMAND_EXEC,
		.help = "enable timestamp",
		.usage = "",
	},
	{
		.name = "disable",
		.handler = handle_disable_command,
		.mode = COMMAND_EXEC,
		.help = "disable timestamp",
		.usage = "",
	},
	{
		.name = "close",
		.handler = handle_close_command,
		.mode = COMMAND_EXEC,
		.help = "close timestamp hardware",
		.usage = "",
	},
	COMMAND_REGISTRATION_DONE
};

const struct command_registration timestamp_command_group_handlers[] = {
       {
               .name = "timestamp",
               .mode = COMMAND_ANY,
               .help = "Timestamp command group",
               .usage = "",
               .chain = timestamp_command_handlers,
       },
       COMMAND_REGISTRATION_DONE
};
