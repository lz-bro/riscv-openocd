// SPDX-License-Identifier: GPL-2.0-or-later

#include <helper/log.h>
#include "trace.h"
#include "target/target.h"
#include "../riscv.h"
#include "../field_helpers.h"

#define TIMESTAMP_CNTCR		0x000
#define TIMESTAMP_CNTSR		0x004
#define TIMESTAMP_CNTCVL	0x008
#define TIMESTAMP_CNTCVU	0x00c

/* register description */
/* TIMESTAMP_CNTCR - 0x000 */
#define TIMESTAMP_CNTCR_EN	1ULL
#define TIMESTAMP_CNTCR_HDBG	2ULL

static int coresight_timestamp_enable(struct target *target)
{
	RISCV_INFO(r);
	struct cs_ts_info *info = r->cs_ts_info;

	if (trace_reg_write(target, TIMESTAMP_CNTCVL + info->base, info->cntval & 0xffffffff) != ERROR_OK)
		return ERROR_FAIL;
	if (trace_reg_write(target, TIMESTAMP_CNTCVU + info->base, info->cntval >> 32) != ERROR_OK)
		return ERROR_FAIL;

	uint32_t cntcr = TIMESTAMP_CNTCR_EN;
	cntcr = set_field32(cntcr, TIMESTAMP_CNTCR_HDBG, info->halt_debug);
	return trace_reg_write(target, TIMESTAMP_CNTCR + info->base, cntcr);
}

static int coresight_timestamp_disable(struct target *target)
{
	RISCV_INFO(r);
	struct cs_ts_info *info = r->cs_ts_info;

	uint32_t cntcr;
	if (trace_reg_read(target, TIMESTAMP_CNTCR + info->base, &cntcr) != ERROR_OK)
		return ERROR_FAIL;
	cntcr = set_field32(cntcr, TIMESTAMP_CNTCR_EN, 0x0);

	return trace_reg_write(target, TIMESTAMP_CNTCR + info->base, cntcr);
}

enum coresight_timestamp_cfg_opts {
	CS_TIMESTAMP_CFG_BASE,
	CS_TIMESTAMP_CFG_HALT_DEBUG,
	CS_TIMESTAMP_CFG_CNT_VAL,
	CS_TIMESTAMP_CFG_INVALID = -1
};

static struct jim_nvp nvp_config_opts[] = {
	{ .name = "-base", .value = CS_TIMESTAMP_CFG_BASE },
	{ .name = "-halt_debug", .value = CS_TIMESTAMP_CFG_HALT_DEBUG },
	{ .name = "-cntval", .value = CS_TIMESTAMP_CFG_CNT_VAL },
	{ .name = NULL, .value = CS_TIMESTAMP_CFG_INVALID }
};

COMMAND_HANDLER(handle_config_command)
{
	if (CMD_ARGC % 2) {
		LOG_ERROR("Command takes an even number of parameters.");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct cs_ts_info *info = r->cs_ts_info;

	if (CMD_ARGC == 0) {
		command_print(CMD, "Base: 0x%" PRIx64, info->base);
		command_print(CMD, "Halt on Debug: %s", info->halt_debug ? "on" : "off");
		command_print(CMD, "Current Counter Value: 0x%" PRIx64, info->cntval);
		return ERROR_OK;
	}

	struct jim_nvp *n;
	for (unsigned int i = 0; i < CMD_ARGC - 1; i += 2) {
		n = jim_nvp_name2value_simple(nvp_config_opts, CMD_ARGV[i]);
		switch (n->value) {
		case CS_TIMESTAMP_CFG_BASE:
			COMMAND_PARSE_ADDRESS(CMD_ARGV[i + 1], info->base);
			break;
		case CS_TIMESTAMP_CFG_HALT_DEBUG:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->halt_debug);
			break;
		case CS_TIMESTAMP_CFG_CNT_VAL:
			COMMAND_PARSE_NUMBER(u64, CMD_ARGV[i + 1], info->cntval);
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
	struct cs_ts_info *info = r->cs_ts_info;

	uint32_t cntcr, cntsr, cntcvl, cntcvu;
	if (trace_reg_read(target, TIMESTAMP_CNTCR + info->base, &cntcr) != ERROR_OK)
		return ERROR_FAIL;
	if (trace_reg_read(target, TIMESTAMP_CNTSR + info->base, &cntsr) != ERROR_OK)
		return ERROR_FAIL;
        if (trace_reg_read(target, TIMESTAMP_CNTCVL + info->base, &cntcvl) != ERROR_OK)
                return ERROR_FAIL;
        if (trace_reg_read(target, TIMESTAMP_CNTCVU + info->base, &cntcvu) != ERROR_OK)
                return ERROR_FAIL;


	command_print(CMD, "coresight-timestamp @0x%" PRIx64, info->base);
	command_print(CMD, "    cntcr=0x%x", cntcr);
	command_print(CMD, "    cntsr=0x%x", cntsr);
	command_print(CMD, "    cntcvl=0x%x", cntcvl);
	command_print(CMD, "    cntcvu=0x%x", cntcvu);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_enable_command)
{
	struct target *target = get_current_target(CMD_CTX);

	if (coresight_timestamp_enable(target) != ERROR_OK) {
		LOG_TARGET_ERROR(target, "Failed to enable Coresight Timestamp");
		return ERROR_FAIL;
	}
	LOG_TARGET_INFO(target, "Coresight Timestamp enabled");
	return ERROR_OK;
}

COMMAND_HANDLER(handle_disable_command)
{
	struct target *target = get_current_target(CMD_CTX);

	if (coresight_timestamp_disable(target) != ERROR_OK) {
		LOG_TARGET_ERROR(target, "Failed to disable Coresight Timestamp");
		return ERROR_FAIL;
	}
	LOG_TARGET_INFO(target, "Coresight Timestamp disabled");
	return ERROR_OK;
}

const struct command_registration coresight_timestamp_command_handlers[] = {
	{
		.name = "config",
		.handler = handle_config_command,
		.mode = COMMAND_ANY,
		.help = "Configuration coresight timestamp.",
		.usage = "coresight_timestamp_attribute ...",
	},
	{
		.name = "info",
		.handler = handle_info_command,
		.mode = COMMAND_ANY,
		.help = "display coresight timestamp info.",
		.usage = "",
	},
	{
		.name = "enable",
		.handler = handle_enable_command,
		.mode = COMMAND_EXEC,
		.help = "enable coresight timestamp",
		.usage = "",
	},
	{
		.name = "disable",
		.handler = handle_disable_command,
		.mode = COMMAND_EXEC,
		.help = "disable coresight timestamp",
		.usage = "",
	},
	COMMAND_REGISTRATION_DONE
};

const struct command_registration coresight_timestamp_command_group_handlers[] = {
       {
               .name = "coresight-timestamp",
               .mode = COMMAND_ANY,
               .help = "Coresight Timestamp command group",
               .usage = "",
               .chain = coresight_timestamp_command_handlers,
       },
       COMMAND_REGISTRATION_DONE
};
