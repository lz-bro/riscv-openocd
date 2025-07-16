// SPDX-License-Identifier: GPL-2.0-or-later

#include <helper/log.h>
#include "trace.h"
#include "target/target.h"
#include "../riscv.h"
#include "../field_helpers.h"

#define FUNNEL_FUNCTL		0x000
#define FUNNEL_PRICTL		0x004

#define FUNNEL_HOLDTIME_MASK	0xf00
#define FUNNEL_HOLDTIME		0x7
#define FUNNEL_ENSx_MASK	0xff

#define FUNNEL_LAR		0xfb0
#define FUNNEL_UNLOCK		0xc5acce55

static int coresight_funnel_enable(struct target *target)
{
	RISCV_INFO(r);
	struct cs_funnel_info *info = r->cs_funnel_info;

	if (trace_reg_write(target, FUNNEL_LAR + info->base, FUNNEL_UNLOCK) != ERROR_OK)
		return ERROR_FAIL;

	uint32_t functl;
	if (trace_reg_read(target, FUNNEL_FUNCTL + info->base, &functl) != ERROR_OK)
		return ERROR_FAIL;
	functl = set_field32(functl, FUNNEL_HOLDTIME_MASK, FUNNEL_HOLDTIME);
	functl |= (info->port);
	if (trace_reg_write(target, FUNNEL_FUNCTL + info->base, functl) != ERROR_OK)
		return ERROR_FAIL;

	if (!info->priority && trace_reg_write(target, FUNNEL_PRICTL + info->base, info->priority) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, FUNNEL_LAR + info->base, 0x0) != ERROR_OK)
		return ERROR_FAIL;

	info->was_enabled = true;
	return ERROR_OK;
}

static int coresight_funnel_disable(struct target *target)
{
	RISCV_INFO(r);
	struct cs_funnel_info *info = r->cs_funnel_info;

	if (trace_reg_write(target, FUNNEL_LAR + info->base, FUNNEL_UNLOCK) != ERROR_OK)
		return ERROR_FAIL;

	uint32_t functl;
	if (trace_reg_read(target, FUNNEL_FUNCTL + info->base, &functl) != ERROR_OK)
		return ERROR_FAIL;
	functl &= ~(info->port);
	if (trace_reg_write(target, FUNNEL_FUNCTL + info->base, functl) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, FUNNEL_LAR + info->base, 0x0) != ERROR_OK)
		return ERROR_FAIL;

	info->was_enabled = false;
	return ERROR_OK;
}

enum coresight_funnel_cfg_opts {
	CS_FUNNEL_CFG_BASE,
	CS_FUNNEL_CFG_PORT,
	CS_FUNNEL_CFG_PRIORITY,
	CS_FUNNEL_CFG_INVALID = -1
};

static struct jim_nvp nvp_config_opts[] = {
	{ .name = "-base", .value = CS_FUNNEL_CFG_BASE },
	{ .name = "-port", .value = CS_FUNNEL_CFG_PORT },
	{ .name = "-priority", .value = CS_FUNNEL_CFG_PRIORITY },
	{ .name = NULL, .value = CS_FUNNEL_CFG_INVALID }
};

COMMAND_HANDLER(handle_config_command)
{
	if (CMD_ARGC % 2) {
		LOG_ERROR("Command takes an even number of parameters.");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct cs_funnel_info *info = r->cs_funnel_info;

	if (CMD_ARGC == 0) {
		command_print(CMD, "Coresight Funnel Base: 0x%" PRIx64, info->base);
		command_print(CMD, "Coresight Funnel Port: 0x%x", info->port);
		command_print(CMD, "Coresight Funnel Priority: 0x%x", info->priority);
	}

	struct jim_nvp *n;
	for (unsigned int i = 0; i < CMD_ARGC - 1; i += 2) {
		n = jim_nvp_name2value_simple(nvp_config_opts, CMD_ARGV[i]);
		switch (n->value) {
		case CS_FUNNEL_CFG_BASE:
			COMMAND_PARSE_ADDRESS(CMD_ARGV[i + 1], info->base);
			break;
		case CS_FUNNEL_CFG_PORT:
			COMMAND_PARSE_NUMBER(u32, CMD_ARGV[i + 1], info->port);
			break;
		case CS_FUNNEL_CFG_PRIORITY:
			COMMAND_PARSE_NUMBER(u32, CMD_ARGV[i + 1], info->priority);
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
	struct cs_funnel_info *info = r->cs_funnel_info;

	if (trace_reg_write(target, FUNNEL_LAR + info->base, FUNNEL_UNLOCK) != ERROR_OK)
		return ERROR_FAIL;

	uint32_t functl, priority;
	if (trace_reg_read(target, FUNNEL_FUNCTL + info->base, &functl) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_read(target, FUNNEL_PRICTL + info->base, &priority) != ERROR_OK)
		return ERROR_FAIL;

	command_print(CMD, "coresight-funnel @0x%" PRIx64, info->base);
	command_print(CMD, "    functl=0x%x", functl);
	command_print(CMD, "    priority=0x%x", info->port);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_enable_command)
{
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct cs_funnel_info *info = r->cs_funnel_info;

	if (!info->was_enabled && coresight_funnel_enable(target) != ERROR_OK) {
		LOG_TARGET_ERROR(target, "Failed to enable Coresight Funnel %d.", info->port);
		return ERROR_FAIL;
	}
	LOG_TARGET_INFO(target, "Coresight Funnel %d enabled.", info->port);
	return ERROR_OK;
}

COMMAND_HANDLER(handle_disable_command)
{
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct cs_funnel_info *info = r->cs_funnel_info;

	if (!info->was_enabled) {
		LOG_TARGET_INFO(target, "Coresight Funnel %d already disable.", info->port);
		return ERROR_OK;
	}

	if (coresight_funnel_disable(target) != ERROR_OK) {
		LOG_TARGET_ERROR(target, "Failed to disable Coresight Funnel %d.", info->port);
		return ERROR_FAIL;
	}
	LOG_TARGET_INFO(target, "Coresight Funnel %d disabled.", info->port);
	return ERROR_OK;
}

const struct command_registration coresight_funnel_command_handlers[] = {
	{
		.name = "config",
		.handler = handle_config_command,
		.mode = COMMAND_ANY,
		.help = "Configuration coresight funnel.",
		.usage = "coresight_funnel_attribute ...",
	},
	{
		.name = "info",
		.handler = handle_info_command,
		.mode = COMMAND_ANY,
		.help = "display coresight funnel info.",
		.usage = "",
	},
	{
		.name = "enable",
		.handler = handle_enable_command,
		.mode = COMMAND_EXEC,
		.help = "enable coresight funnel",
		.usage = "",
	},
	{
		.name = "disable",
		.handler = handle_disable_command,
		.mode = COMMAND_EXEC,
		.help = "disable coresight funnel",
		.usage = "",
	},
	COMMAND_REGISTRATION_DONE
};

const struct command_registration coresight_funnel_command_group_handlers[] = {
       {
               .name = "coresight-funnel",
               .mode = COMMAND_ANY,
               .help = "Coresight Funnel command group",
               .usage = "",
               .chain = coresight_funnel_command_handlers,
       },
       COMMAND_REGISTRATION_DONE
};
