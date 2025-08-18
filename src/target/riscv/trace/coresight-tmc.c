// SPDX-License-Identifier: GPL-2.0-or-later

#include <time.h>
#include <helper/log.h>
#include <helper/fileio.h>
#include <helper/time_support.h>
#include "coresight-tmc.h"
#include "trace.h"
#include "trace_defines.h"
#include "target/target.h"
#include "../field_helpers.h"

#define MAX_TRACE_BUFSIZE 0x100000000

int tmc_read_rrp(struct target *target, target_addr_t tmc_base, uint64_t *rrp)
{
	uint32_t value;
	if (trace_reg_read(target, TMC_RRP + tmc_base, &value) != ERROR_OK)
		return ERROR_FAIL;
	*rrp = value;

	if (trace_reg_read(target, TMC_RRPHI + tmc_base, &value) != ERROR_OK)
		return ERROR_FAIL;
	*rrp |= (uint64_t)value << 32;

	return ERROR_OK;
}

int tmc_read_rwp(struct target *target, target_addr_t tmc_base, uint64_t *rwp)
{
	uint32_t value;
	if (trace_reg_read(target, TMC_RWP + tmc_base, &value) != ERROR_OK)
		return ERROR_FAIL;
	*rwp = value;

	if (trace_reg_read(target, TMC_RWPHI + tmc_base, &value) != ERROR_OK)
		return ERROR_FAIL;
	*rwp |= (uint64_t)value << 32;

	return ERROR_OK;
}

int tmc_write_rrp(struct target *target, target_addr_t tmc_base, uint64_t rrp)
{
	return trace_reg_write(target, TMC_RRP + tmc_base, rrp & 0xffffffff) &&
			trace_reg_write(target, TMC_RRPHI + tmc_base, rrp >> 32);
}

int tmc_write_rwp(struct target *target, target_addr_t tmc_base, uint64_t rwp)
{
	return trace_reg_write(target, TMC_RWP + tmc_base, rwp & 0xffffffff) &&
			trace_reg_write(target, TMC_RWPHI + tmc_base, rwp >> 32);
}

int tmc_wait_for_tmcready(struct target *target, target_addr_t tmc_base)
{
	uint32_t sts;
	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for TMC to become ready.");
	while (1) {
		if (trace_reg_read(target, TMC_STS + tmc_base, &sts) != ERROR_OK)
			return ERROR_FAIL;
		if (get_field32(sts, TMC_STS_TMCREADY))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			LOG_TARGET_ERROR(target, "TMC (at address base=0x%" TARGET_PRIxADDR ") did not become ready in %d s. "
					"Increase the timeout with 'riscv set_command_timeout_sec'.",
					tmc_base, riscv_get_command_timeout_sec());
			return ERROR_TIMEOUT_REACHED;
		}
	}
	return ERROR_OK;
}

int tmc_flush_and_stop(struct target *target, target_addr_t tmc_base)
{
	uint32_t ffcr;
	if (trace_reg_read(target, TMC_FFCR + tmc_base, &ffcr) != ERROR_OK)
		return ERROR_FAIL;

	ffcr |= TMC_FFCR_STOP_ON_FLUSH;
	if (trace_reg_write(target, TMC_FFCR + tmc_base, ffcr) != ERROR_OK)
		return ERROR_FAIL;

	ffcr |= TMC_FFCR_FLUSHMAN;
	if (trace_reg_write(target, TMC_FFCR + tmc_base, ffcr) != ERROR_OK)
		return ERROR_FAIL;

	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for completion of Manual Flush.");
	while (1) {
		if (trace_reg_read(target, TMC_FFCR + tmc_base, &ffcr) != ERROR_OK)
			return ERROR_FAIL;
		if (!get_field32(ffcr, TMC_FFCR_FLUSHMAN))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			LOG_TARGET_ERROR(target, "Timeout while waiting for completion of Manual Flush in %d s. "
					"Increase the timeout with 'riscv set_command_timeout_sec'.",
					riscv_get_command_timeout_sec());
			return ERROR_TIMEOUT_REACHED;
		}
	}

	return tmc_wait_for_tmcready(target, tmc_base);
}

int tmc_sync_etr_buf(struct target *target)
{
	RISCV_INFO(r);
	struct cs_tmc_info *info = r->cs_tmc_info;

	uint64_t rrp, rwp;
	uint32_t status;

	if (tmc_read_rrp(target, info->etr_base, &rrp) != ERROR_OK)
		return ERROR_FAIL;
	if (tmc_read_rwp(target, info->etr_base, &rwp) != ERROR_OK)
		return ERROR_FAIL;

	info->buf_size = rwp - rrp;

	if (trace_reg_read(target, TMC_STS + info->etr_base, &status) != ERROR_OK)
		return ERROR_FAIL;
	if (get_field32(status, TMC_STS_FULL))
		LOG_TARGET_INFO(target, "Trace data overflow, only 0x%" PRIx64 " bytes is valid",
				info->buf_size);
	if (get_field32(status, TMC_STS_MEMERR)) {
		LOG_TARGET_ERROR(target, "TMC memory error detected");
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

int coresight_etf_enable(struct target *target)
{
	RISCV_INFO(r);
	struct cs_tmc_info *info = r->cs_tmc_info;

	if (trace_reg_write(target, TMC_LAR + info->etf_base, TMC_UNLOCK) != ERROR_OK)
		return ERROR_FAIL;

	if (tmc_wait_for_tmcready(target, info->etf_base) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, TMC_MODE + info->etf_base, TMC_MODE_HARDWARE_FIFO) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, TMC_FFCR + info->etf_base, TMC_FFCR_EN_FMT | TMC_FFCR_EN_TI) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, TMC_BUFWM + info->etf_base, 0x0) != ERROR_OK)
		return ERROR_FAIL;

	// enable etf
	if (trace_reg_write(target, TMC_CTL + info->etf_base, TMC_CTL_CAPT_EN) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, TMC_LAR + info->etf_base, 0x0) != ERROR_OK)
		return ERROR_FAIL;

	return ERROR_OK;
}

int coresight_etf_disable(struct target *target)
{
	RISCV_INFO(r);
	struct cs_tmc_info *info = r->cs_tmc_info;

	if (trace_reg_write(target, TMC_LAR + info->etf_base, TMC_UNLOCK) != ERROR_OK)
		return ERROR_FAIL;

	if (tmc_flush_and_stop(target, info->etf_base) != ERROR_OK)
		return ERROR_FAIL;

	// disable etf
	if (trace_reg_write(target, TMC_CTL + info->etf_base, 0x0) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, TMC_LAR + info->etf_base, 0x0) != ERROR_OK)
		return ERROR_FAIL;

	return ERROR_OK;
}

int coresight_etr_enable(struct target *target)
{
	RISCV_INFO(r);
	struct cs_tmc_info *info = r->cs_tmc_info;

	if (trace_reg_write(target, TMC_LAR + info->etr_base, TMC_UNLOCK) != ERROR_OK)
		return ERROR_FAIL;

	if (tmc_wait_for_tmcready(target, info->etr_base) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, TMC_RSZ + info->etr_base, info->ram_size / 4) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, TMC_MODE + info->etr_base, TMC_MODE_CIRCULAR_BUFFER) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, TMC_FFCR + info->etr_base, TMC_FFCR_EN_FMT | TMC_FFCR_EN_TI) != ERROR_OK)
		return ERROR_FAIL;

	// DBALO/DBAHI to base address
	if (trace_reg_write(target, TMC_DBALO + info->etr_base, info->hwaddr & 0xffffffff) != ERROR_OK)
		return ERROR_FAIL;
	if (trace_reg_write(target, TMC_DBAHI + info->etr_base, info->hwaddr >> 32) != ERROR_OK)
		return ERROR_FAIL;

	// RRP/RWP to base address
	if (tmc_write_rrp(target, info->etr_base, info->hwaddr) != ERROR_OK)
		return ERROR_FAIL;
	if (tmc_write_rwp(target, info->etr_base, info->hwaddr) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, TMC_AXICTL + info->etr_base, 0x0) != ERROR_OK)
		return ERROR_FAIL;

	// enable etr
	if (trace_reg_write(target, TMC_CTL + info->etr_base, TMC_CTL_CAPT_EN) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, TMC_LAR + info->etr_base, 0x0) != ERROR_OK)
		return ERROR_FAIL;

	return ERROR_OK;
}

int coresight_etr_disable(struct target *target)
{
	RISCV_INFO(r);
	struct cs_tmc_info *info = r->cs_tmc_info;

	if (trace_reg_write(target, TMC_LAR + info->etr_base, TMC_UNLOCK) != ERROR_OK)
		return ERROR_FAIL;

	if (tmc_flush_and_stop(target, info->etr_base) != ERROR_OK)
		return ERROR_FAIL;

	if (tmc_sync_etr_buf(target) != ERROR_OK)
		return ERROR_FAIL;

	// disable etr
	if (trace_reg_write(target, TMC_CTL + info->etr_base, 0x0) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, TMC_LAR + info->etr_base, 0x0) != ERROR_OK)
		return ERROR_FAIL;

	return ERROR_OK;
}

static struct jim_nvp nvp_config_opts[] = {
	{ .name = "-etfbase", .value = CS_TMC_CFG_ETF_BASE },
	{ .name = "-etrbase", .value = CS_TMC_CFG_ETR_BASE },
	{ .name = "-hwaddr", .value = CS_TMC_CFG_HWADDR },
	{ .name = "-size", .value = CS_TMC_CFG_SIZE },
	{ .name = NULL, .value = CS_TMC_CFG_INVALID }
};

static struct jim_nvp nvp_type_opts[] = {
	{ .name = "etb", .value = TMC_TYPE_ETB },
	{ .name = "etf", .value = TMC_TYPE_ETF },
	{ .name = "etr", .value = TMC_TYPE_ETR },
};

COMMAND_HANDLER(handle_config_command)
{
	if (CMD_ARGC % 2) {
		LOG_ERROR("Command takes an even number of parameters.");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct cs_tmc_info *info = r->cs_tmc_info;

	if (CMD_ARGC == 0) {
		command_print(CMD, "ETF Base: 0x%" PRIx64, info->etf_base);
		command_print(CMD, "ETR Base: 0x%" PRIx64, info->etr_base);
		command_print(CMD, "Data Buffer Address: 0x%" PRIx64, info->hwaddr);
		command_print(CMD, "Ram Size: 0x%" PRIx64, info->ram_size);
		return ERROR_OK;
	}

	struct jim_nvp *n;
	for (unsigned int i = 0; i < CMD_ARGC - 1; i += 2) {
		n = jim_nvp_name2value_simple(nvp_config_opts, CMD_ARGV[i]);
		switch (n->value) {
		case CS_TMC_CFG_ETF_BASE:
			COMMAND_PARSE_ADDRESS(CMD_ARGV[i + 1], info->etf_base);
			break;
		case CS_TMC_CFG_ETR_BASE:
			COMMAND_PARSE_ADDRESS(CMD_ARGV[i + 1], info->etr_base);
			break;
		case CS_TMC_CFG_HWADDR:
			COMMAND_PARSE_ADDRESS(CMD_ARGV[i + 1], info->hwaddr);
			break;
		case CS_TMC_CFG_SIZE:
			COMMAND_PARSE_NUMBER(u64, CMD_ARGV[i + 1], info->ram_size);
			if (info->ram_size > MAX_TRACE_BUFSIZE) {
				LOG_ERROR("The maximum trace buffer size permitted is 4GB.");
				return ERROR_COMMAND_ARGUMENT_INVALID;
			}
			break;
		default:
			return ERROR_COMMAND_SYNTAX_ERROR;
		}
	}
	return ERROR_OK;
}

COMMAND_HELPER(coresight_tmc_status, target_addr_t tmc_base)
{
	struct target *target = get_current_target(CMD_CTX);
	uint32_t rsz, sts, ctl, ffsr, ffcr;
	uint64_t rrp, rwp;

	if (trace_reg_write(target, TMC_LAR + tmc_base, TMC_UNLOCK) != ERROR_OK)
		return ERROR_FAIL;

	if (tmc_read_rrp(target, tmc_base, &rrp) != ERROR_OK)
		return ERROR_FAIL;
	if (tmc_read_rwp(target, tmc_base, &rwp) != ERROR_OK)
		return ERROR_FAIL;
	if (trace_reg_read(target, TMC_RSZ + tmc_base, &rsz) != ERROR_OK)
		return ERROR_FAIL;
	if (trace_reg_read(target, TMC_STS + tmc_base, &sts) != ERROR_OK)
		return ERROR_FAIL;
	if (trace_reg_read(target, TMC_CTL + tmc_base, &ctl) != ERROR_OK)
		return ERROR_FAIL;
	if (trace_reg_read(target, TMC_FFSR + tmc_base, &ffsr) != ERROR_OK)
		return ERROR_FAIL;
	if (trace_reg_read(target, TMC_FFCR + tmc_base, &ffcr) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_write(target, TMC_LAR + tmc_base, 0x0) != ERROR_OK)
		return ERROR_FAIL;

	command_print(CMD, "coresight-%s @0x%" PRIx64, CMD_ARGV[0], tmc_base);
	command_print(CMD, "    rsz=0x%" PRIx64, (uint64_t)rsz * 4);
	command_print(CMD, "    sts=0x%x", sts);
	command_print(CMD, "    rrp=0x%" PRIx64, rrp);
	command_print(CMD, "    rwp=0x%" PRIx64, rwp);
	command_print(CMD, "    ctl=0x%x", ctl);
	command_print(CMD, "    ffsr=0x%x", ffsr);
	command_print(CMD, "    ffcr=0x%x", ffcr);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_info_command)
{
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct cs_tmc_info *info = r->cs_tmc_info;

	if (CMD_ARGC != 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	target_addr_t tmc_base;
	struct jim_nvp *n = jim_nvp_name2value_simple(nvp_type_opts, CMD_ARGV[0]);
	switch (n->value) {
	case TMC_TYPE_ETF:
		tmc_base = info->etf_base;
		break;
	case TMC_TYPE_ETR:
		tmc_base = info->etr_base;
		break;
	default:
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	return coresight_tmc_status(CMD, tmc_base);
}

COMMAND_HANDLER(handle_enable_command)
{
	struct target *target = get_current_target(CMD_CTX);
	if (CMD_ARGC != 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct jim_nvp *n = jim_nvp_name2value_simple(nvp_type_opts, CMD_ARGV[0]);
	switch (n->value) {
	case TMC_TYPE_ETF:
		if (coresight_etf_enable(target) != ERROR_OK) {

			LOG_TARGET_ERROR(target, "Failed to enable ETF.");
			return ERROR_FAIL;
		}
		LOG_TARGET_INFO(target, "ETF enabled.");
		break;
	case TMC_TYPE_ETR:
		if (coresight_etr_enable(target) != ERROR_OK) {
			LOG_TARGET_ERROR(target, "Failed to enable ETR.");
			return ERROR_FAIL;
		}
		LOG_TARGET_INFO(target, "ETR enabled.");
		break;
	default:
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	return ERROR_OK;
}

COMMAND_HANDLER(handle_disable_command)
{
	struct target *target = get_current_target(CMD_CTX);
	if (CMD_ARGC != 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct jim_nvp *n = jim_nvp_name2value_simple(nvp_type_opts, CMD_ARGV[0]);
	switch (n->value) {
	case TMC_TYPE_ETF:
		if (coresight_etf_disable(target) != ERROR_OK) {
			LOG_TARGET_ERROR(target, "Failed to disable ETF.");
			return ERROR_FAIL;
		}
		LOG_TARGET_INFO(target, "ETF disabled.");
		break;
	case TMC_TYPE_ETR:
		if (coresight_etr_disable(target) != ERROR_OK) {
			LOG_TARGET_ERROR(target, "Failed to disable ETR.");
			return ERROR_FAIL;
		}
		LOG_TARGET_INFO(target, "ETR disabled.");
		break;
	default:
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	return ERROR_OK;
}

COMMAND_HANDLER(handle_dump_command)
{
	struct fileio *fileio;
	uint8_t *buffer;
	int retval, retvaltemp;
	target_addr_t address, size;
	struct duration bench;

	if (CMD_ARGC != 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct cs_tmc_info *info = r->cs_tmc_info;

	address = info->hwaddr;
	size = info->buf_size;

	uint32_t buf_size = (size > 4096) ? 4096 : size;
	buffer = malloc(buf_size);
	if (!buffer)
		return ERROR_FAIL;

	retval = fileio_open(&fileio, CMD_ARGV[0], FILEIO_WRITE, FILEIO_BINARY);
	if (retval != ERROR_OK) {
		free(buffer);
		return retval;
	}

	duration_start(&bench);

	while (size > 0) {
		size_t size_written;
		uint32_t this_run_size = (size > buf_size) ? buf_size : size;
		retval = target_read_phys_buffer(target, address, this_run_size, buffer);
		if (retval != ERROR_OK)
			break;

		retval = fileio_write(fileio, this_run_size, buffer, &size_written);
		if (retval != ERROR_OK)
			break;

		size -= this_run_size;
		address += this_run_size;
	}

	free(buffer);

	if ((retval == ERROR_OK) && (duration_measure(&bench) == ERROR_OK)) {
		size_t filesize;
		retval = fileio_size(fileio, &filesize);
		if (retval != ERROR_OK)
			return retval;
		command_print(CMD,
				"dumped %zu bytes in %fs (%0.3f KiB/s)", filesize,
				duration_elapsed(&bench), duration_kbps(&bench, filesize));
	}

	retvaltemp = fileio_close(fileio);
	if (retvaltemp != ERROR_OK)
		return retvaltemp;

	return retval;
}

const struct command_registration coresight_tmc_command_handlers[] = {
	{
		.name = "config",
		.handler = handle_config_command,
		.mode = COMMAND_ANY,
		.help = "Configuration coresight tmc.",
		.usage = "coresight_tmc_attribute ...",
	},
	{
		.name = "info",
		.handler = handle_info_command,
		.mode = COMMAND_ANY,
		.help = "display coresight tmc info.",
		.usage = "",
	},
	{
		.name = "enable",
		.handler = handle_enable_command,
		.mode = COMMAND_EXEC,
		.help = "enable coresight tmc",
		.usage = "etf|etr",
	},
	{
		.name = "disable",
		.handler = handle_disable_command,
		.mode = COMMAND_EXEC,
		.help = "disable coresight tmc",
		.usage = "etf|etr",
	},
	{
		.name = "dump",
		.handler = handle_dump_command,
		.mode = COMMAND_EXEC,
		.help = "dump trace buffer",
		.usage = "filename",
	},
	COMMAND_REGISTRATION_DONE
};

const struct command_registration coresight_tmc_command_group_handlers[] = {
       {
               .name = "coresight-tmc",
               .mode = COMMAND_ANY,
               .help = "Coresight TMC command group",
               .usage = "",
               .chain = coresight_tmc_command_handlers,
       },
       COMMAND_REGISTRATION_DONE
};
