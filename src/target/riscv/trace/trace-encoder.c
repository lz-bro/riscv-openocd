// SPDX-License-Identifier: GPL-2.0-or-later
#include <time.h>

#include <helper/log.h>
#include "trace.h"
#include "trace_defines.h"
#include "target/target.h"
#include "../riscv.h"
#include "../field_helpers.h"

static int activate_encoder(struct target *target)
{
	RISCV_INFO(r);
	struct encoder_info *info = r->encoder_info;

	LOG_TARGET_DEBUG(target, "Activating the Trace Encoder with base address = 0x%" TARGET_PRIxADDR, info->base);

	uint32_t control;
	if (trace_reg_write(target, TE_CONTROL + info->base, TE_CONTROL_ACTIVE_ACTIVE) != ERROR_OK)
		return ERROR_FAIL;

	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for the Trace Encoder to become active.");
	while (1) {
		if (trace_reg_read(target, TE_CONTROL + info->base, &control) != ERROR_OK)
			return ERROR_FAIL;
		if (get_field32(control, TE_CONTROL_ACTIVE))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			LOG_TARGET_ERROR(target, "Trace Encoder (at address base=0x%" TARGET_PRIxADDR ") did not become active in %d s. "
					"Increase the timeout with 'riscv set_command_timeout_sec'.",
					info->base, riscv_get_command_timeout_sec());
			return ERROR_TIMEOUT_REACHED;
		}
	}
	LOG_TARGET_DEBUG(target, "Trace Encoder has become active.");

	uint32_t impl;
	if (trace_reg_read(target, TE_IMPL + info->base, &impl) != ERROR_OK)
		return ERROR_FAIL;

	if (get_field32(impl, TE_IMPL_VERMAJOR) != 0x1) {
		LOG_TARGET_ERROR(target, "the component is not compliant with version 1.0");
		return ERROR_FAIL;
	}

	if (get_field32(impl, TE_IMPL_COMPTYPE) != TE_IMPL_COMPTYPE_FLAG) {
		LOG_TARGET_ERROR(target, "Trace Encoder Component Type does not match expected value");
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

static int deactivate_encoder(struct target *target)
{
	RISCV_INFO(r);
	struct encoder_info *info = r->encoder_info;

	uint32_t control;
	if (trace_reg_read(target, TE_CONTROL + info->base, &control) != ERROR_OK)
		return ERROR_FAIL;
	if (get_field32(control, TE_CONTROL_ACTIVE)) {
		LOG_TARGET_DEBUG(target, "Initiating Trace encoder reset.");
		if (trace_reg_write(target, TE_CONTROL + info->base, TE_CONTROL_ACTIVE_INACTIVE) != ERROR_OK)
			return ERROR_FAIL;
		const time_t start = time(NULL);
		LOG_TARGET_DEBUG(target, "Waiting for the Trace encoder to become reset state.");
		while (1) {
			if (trace_reg_read(target, TE_CONTROL + info->base, &control) != ERROR_OK)
				return ERROR_FAIL;
			if (!get_field32(control, TE_CONTROL_ACTIVE))
				break;
			if (time(NULL) - start > riscv_get_command_timeout_sec()) {
				LOG_TARGET_ERROR(target, "Trace Encoder (at address base=0x%" TARGET_PRIxADDR ") did not become reset state in %d s. "
						"Increase the timeout with 'riscv set_command_timeout_sec'.",
						info->base, riscv_get_command_timeout_sec());
				return ERROR_TIMEOUT_REACHED;
			}
		}
	}
	LOG_TARGET_DEBUG(target, "Trace Encoder has become reset state.");
	return ERROR_OK;
}

static int reset_encoder(struct target *target)
{
	RISCV_INFO(r);
	struct encoder_info *info = r->encoder_info;

	if (deactivate_encoder(target) != ERROR_OK)
		return ERROR_FAIL;
	if (activate_encoder(target) != ERROR_OK)
		return ERROR_FAIL;
	info->was_reset = true;

	LOG_TARGET_DEBUG(target, "Trace Encoder successfully reset.");
	return ERROR_OK;
}

static int set_feature_config(struct target *target)
{
	RISCV_INFO(r);
	struct encoder_info *info = r->encoder_info;

	uint32_t feature = set_field32(0, TE_INST_FEATURES_NOADDRDIFF, info->noaddr_diff ? 1 : 0);
	feature = set_field32(feature, TE_INST_FEATURES_NOTRAPADDR, info->notrap_addr ? 1 : 0);
	feature = set_field32(feature, TE_INST_FEATURES_ENSEQUENTIALJUMP, info->seq_jump ? 1 : 0);
	feature = set_field32(feature, TE_INST_FEATURES_ENIMPLICITRETURN, info->implicit_return ? 1 : 0);
	feature = set_field32(feature, TE_INST_FEATURES_ENBRANCHPREDICTION, info->branch_prediction ? 1 : 0);
	feature = set_field32(feature, TE_INST_FEATURES_ENJUMPTARGETCACHE, info->jump_target_cache ? 1 : 0);
	feature = set_field32(feature, TE_INST_FEATURES_IMPLICITRETURNMODE, info->implicit_return_mode);
	feature = set_field32(feature, TE_INST_FEATURES_ENREPEATEDHISTORY, info->repeated_history);
	feature = set_field32(feature, TE_INST_FEATURES_ENALLJUMPS, info->all_jumps);
	feature = set_field32(feature, TE_INST_FEATURES_EXTENDADDRMSB, info->ext_msb);
	feature = set_field32(feature, TE_INST_FEATURES_SRCID, info->srcid);
	feature = set_field32(feature, TE_INST_FEATURES_SRCBITS, info->srcbits);

	if (trace_reg_write(target, TE_INST_FEATURES + info->base, feature) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_read(target, TE_INST_FEATURES + info->base, &feature) != ERROR_OK)
		return ERROR_FAIL;

	if (get_field32(feature, TE_INST_FEATURES_NOADDRDIFF) != info->noaddr_diff) {
		LOG_TARGET_WARNING(target, "trTeInstNoAddrDiff % is not supported.", info->noaddr_diff);
		info->noaddr_diff = get_field32(feature, TE_INST_FEATURES_NOADDRDIFF);
	}

	if (get_field32(feature, TE_INST_FEATURES_NOTRAPADDR) != info->notrap_addr) {
		LOG_TARGET_WARNING(target, "trTeInstNoTrapAddr % is not supported.", info->notrap_addr);
		info->notrap_addr = get_field32(feature, TE_INST_FEATURES_NOTRAPADDR);
	}

	if (get_field32(feature, TE_INST_FEATURES_ENSEQUENTIALJUMP) != info->seq_jump) {
		LOG_TARGET_WARNING(target, "seq_jump % is not supported.", info->seq_jump);
		info->seq_jump = get_field32(feature, TE_INST_FEATURES_ENSEQUENTIALJUMP);
	}

	if (get_field32(feature, TE_INST_FEATURES_ENIMPLICITRETURN) != info->implicit_return) {
		LOG_TARGET_WARNING(target, "trTeInstEnImplicitReturn % is not supported.", info->implicit_return);
		info->implicit_return = get_field32(feature, TE_INST_FEATURES_ENIMPLICITRETURN);
	}

	if (get_field32(feature, TE_INST_FEATURES_ENBRANCHPREDICTION) != info->branch_prediction) {
		LOG_TARGET_WARNING(target, "trTeInstEnBranchPrediction % is not supported.", info->branch_prediction);
		info->branch_prediction = get_field32(feature, TE_INST_FEATURES_ENBRANCHPREDICTION);
	}

	if (get_field32(feature, TE_INST_FEATURES_ENJUMPTARGETCACHE) != info->jump_target_cache) {
		LOG_TARGET_WARNING(target, "trTeInstEnJumpTargetCache % is not supported.", info->jump_target_cache);
		info->jump_target_cache = get_field32(feature, TE_INST_FEATURES_ENJUMPTARGETCACHE);
	}

	if (get_field32(feature, TE_INST_FEATURES_IMPLICITRETURNMODE) != info->implicit_return_mode) {
		LOG_TARGET_WARNING(target, "trTeInstImplicitReturnMode % is not supported.", info->implicit_return_mode);
		info->implicit_return_mode = get_field32(feature, TE_INST_FEATURES_IMPLICITRETURNMODE);
	}

	if (get_field32(feature, TE_INST_FEATURES_ENREPEATEDHISTORY) != info->repeated_history) {
		LOG_TARGET_WARNING(target, "trTeInstEnRepeatedHistory % is not supported.", info->repeated_history);
		info->repeated_history = get_field32(feature, TE_INST_FEATURES_ENREPEATEDHISTORY);
	}

	if (get_field32(feature, TE_INST_FEATURES_ENALLJUMPS) != info->all_jumps) {
		LOG_TARGET_WARNING(target, "trTeInstEnAllJumps % is not supported.", info->all_jumps);
		info->all_jumps = get_field32(feature, TE_INST_FEATURES_ENALLJUMPS);
	}

	if (get_field32(feature, TE_INST_FEATURES_EXTENDADDRMSB) != info->ext_msb) {
		LOG_TARGET_WARNING(target, "trTeInstExtendAddrMSB % is not supported.", info->ext_msb);
		info->ext_msb = get_field32(feature, TE_INST_FEATURES_EXTENDADDRMSB);
	}

	if (get_field32(feature, TE_INST_FEATURES_SRCID) != info->srcid) {
		LOG_TARGET_WARNING(target, "trTeInstEnBranchPrediction % is not supported.", info->srcid);
		info->srcid = get_field32(feature, TE_INST_FEATURES_SRCID);
	}

	if (get_field32(feature, TE_INST_FEATURES_SRCID) != info->srcid) {
		LOG_TARGET_WARNING(target, "trTeInstEnBranchPrediction % is not supported.", info->srcid);
		info->srcid = get_field32(feature, TE_INST_FEATURES_SRCID);
	}

	return ERROR_OK;
}

static int encoder_enable(struct target *target)
{
	RISCV_INFO(r);
	struct encoder_info *info = r->encoder_info;

	if (!info->was_reset && reset_encoder(target) != ERROR_OK)
		return ERROR_FAIL;

	if (set_feature_config(target) != ERROR_OK)
		return ERROR_FAIL;

	uint32_t control = set_field32(0, TE_CONTROL_ACTIVE, TE_CONTROL_ACTIVE_ACTIVE);
	control = set_field32(control, TE_CONTROL_ENABLE, TE_CONTROL_ENABLE_ON);
	control = set_field32(control, TE_CONTROL_INSTMODE, info->inst_mode);
	control = set_field32(control, TE_CONTROL_CONTEXT, info->context);
	control = set_field32(control, TE_CONTROL_INSTTRIENABLE, info->trigger);
	control = set_field32(control, TE_CONTROL_INSTSTALLENA, info->stall);
	control = set_field32(control, TE_CONTROL_INHIBITSRC, info->inhb_src);
	control = set_field32(control, TE_CONTROL_INSTSYNCMODE, info->sync_mode);
	control = set_field32(control, TE_CONTROL_INSTSYNCMAX, info->sync_max);
	control = set_field32(control, TE_CONTROL_FORMAT, info->trc_fmt);

	if (trace_reg_write(target, TE_CONTROL + info->base, control) != ERROR_OK)
		return ERROR_FAIL;

	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for the Trace Encoder to be enabled.");
	while (1) {
		if (trace_reg_read(target, TE_CONTROL + info->base, &control) != ERROR_OK)
			return ERROR_FAIL;
		if (get_field32(control, TE_CONTROL_ENABLE))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			LOG_TARGET_ERROR(target, "Trace Encoder (at address base=0x%" TARGET_PRIxADDR ") did not to be enabled in %d s. "
					"Increase the timeout with 'riscv set_command_timeout_sec'.",
					info->base, riscv_get_command_timeout_sec());
			return ERROR_TIMEOUT_REACHED;
		}
	}

	if (get_field32(control, TE_CONTROL_INSTMODE) != info->inst_mode) {
		LOG_TARGET_WARNING(target, "trTeInstMode % is not supported.", info->inst_mode);
		info->inst_mode = get_field32(control, TE_CONTROL_INSTMODE);
	}

	if (get_field32(control, TE_CONTROL_CONTEXT) != info->context) {
		LOG_TARGET_WARNING(target, "trTeContext % is not supported.", info->context);
		info->context = get_field32(control, TE_CONTROL_CONTEXT);
	}

	if (get_field32(control, TE_CONTROL_INSTTRIENABLE) != info->trigger) {
		LOG_TARGET_WARNING(target, "trTeInstTrigEnable % is not supported.", info->trigger);
		info->trigger = get_field32(control, TE_CONTROL_INSTTRIENABLE);
	}

	if (get_field32(control, TE_CONTROL_INSTSTALLENA) != info->stall) {
		LOG_TARGET_WARNING(target, "trTeInstStallEna % is not supported.", info->stall);
		info->stall = get_field32(control, TE_CONTROL_INSTSTALLENA);
	}

	if (get_field32(control, TE_CONTROL_INHIBITSRC) != info->inhb_src) {
		LOG_TARGET_WARNING(target, "trTeInhibitSrc % is not supported.", info->inhb_src);
		info->inhb_src = get_field32(control, TE_CONTROL_INHIBITSRC);
	}

	if (get_field32(control, TE_CONTROL_INSTSYNCMODE) != info->sync_mode) {
		LOG_TARGET_WARNING(target, "trTeInstSyncMode % is not supported.", info->sync_mode);
		info->sync_mode = get_field32(control, TE_CONTROL_INSTSYNCMODE);
	}

	if (get_field32(control, TE_CONTROL_INSTSYNCMAX) != info->sync_max) {
		LOG_TARGET_WARNING(target, "trTeInstMode % is not supported.", info->sync_max);
		info->sync_max = get_field32(control, TE_CONTROL_INSTSYNCMAX);
	}

	if (get_field32(control, TE_CONTROL_FORMAT) != info->trc_fmt) {
		LOG_TARGET_WARNING(target, "trTeInstMode % is not supported.", info->trc_fmt);
		info->trc_fmt = get_field32(control, TE_CONTROL_FORMAT);
	}

	info->was_enabled = true;
	return ERROR_OK;
}

static int encoder_disable(struct target *target)
{
	RISCV_INFO(r);
	struct encoder_info *info = r->encoder_info;

	uint32_t control;
	if (trace_reg_read(target, TE_CONTROL + info->base, &control) != ERROR_OK)
		return ERROR_FAIL;
	control = set_field32(control, TE_CONTROL_INSTTRACING, TE_CONTROL_INSTTRACING_OFF);
	control = set_field32(control, TE_CONTROL_ENABLE, TE_CONTROL_ENABLE_OFF);

	if (trace_reg_write(target, TE_CONTROL + info->base, control) != ERROR_OK)
		return ERROR_FAIL;

	const time_t start = time(NULL);
	LOG_TARGET_DEBUG(target, "Waiting for the Trace Encoder to be disabled.");
	while (1) {
		if (trace_reg_read(target, TE_CONTROL + info->base, &control) != ERROR_OK)
			return ERROR_FAIL;
		if (!get_field32(control, TE_CONTROL_ENABLE) && get_field32(control, TE_CONTROL_EMPTY))
			break;
		if (time(NULL) - start > riscv_get_command_timeout_sec()) {
			if (get_field32(control, TE_CONTROL_ENABLE))
				LOG_TARGET_ERROR(target, "Trace Encoder (at address base=0x%" TARGET_PRIxADDR ") did not to be disabled in %d s. "
						"Increase the timeout with 'riscv set_command_timeout_sec'.",
						info->base, riscv_get_command_timeout_sec());

			if (!get_field32(control, TE_CONTROL_EMPTY))
				LOG_TARGET_ERROR(target, "Trace Encoder (at address base=0x%" TARGET_PRIxADDR ") generated trace have not been emitted in %d s. "
						"Increase the timeout with 'riscv set_command_timeout_sec'.",
						info->base, riscv_get_command_timeout_sec());

			return ERROR_TIMEOUT_REACHED;
		}
	}

	info->was_enabled = false;
	return ERROR_OK;
}

enum encoder_cfg_opts {
	ENCODER_CFG_BASE,
	ENCODER_CFG_INST_MODE,
	ENCODER_CFG_CONTEXT,
	ENCODER_CFG_TRIGGER,
	ENCODER_CFG_STALL,
	ENCODER_CFG_INHB_SRC,
	ENCODER_CFG_SYNC_MODE,
	ENCODER_CFG_SYNC_MAX,
	ENCODER_CFG_TRC_FMT,
	ENCODER_CFG_NOADDR_DIFF,
	ENCODER_CFG_NOTRAP_ADDR,
	ENCODER_CFG_SEQ_JUMP,
	ENCODER_CFG_IMPLICIT_RETURN,
	ENCODER_CFG_BRANCH_PREDICTION,
	ENCODER_CFG_JUMP_TARGET_CACHE,
	ENCODER_CFG_IMPLICIT_RETURN_MODE,
	ENCODER_CFG_REPEATED_HISTORY,
	ENCODER_CFG_ALL_JUMPS,
	ENCODER_CFG_EXT_MSB,
	ENCODER_CFG_SRCID,
	ENCODER_CFG_SRCBITS,
	ENCODER_CFG_INVALID = -1
};

static struct jim_nvp nvp_config_opts[] = {
	{ .name = "-base", .value = ENCODER_CFG_BASE },
	{ .name = "-inst_mode", .value = ENCODER_CFG_INST_MODE },
	{ .name = "-context", .value = ENCODER_CFG_CONTEXT },
	{ .name = "-trigger", .value = ENCODER_CFG_TRIGGER },
	{ .name = "-stall", .value = ENCODER_CFG_STALL },
	{ .name = "-inhb_src", .value = ENCODER_CFG_INHB_SRC },
	{ .name = "-sync_mode", .value = ENCODER_CFG_SYNC_MODE },
	{ .name = "-sync_max", .value = ENCODER_CFG_SYNC_MAX },
	{ .name = "-trc_fmt", .value = ENCODER_CFG_TRC_FMT },
	{ .name = "-noaddr_diff", .value = ENCODER_CFG_NOADDR_DIFF },
	{ .name = "-notrap_addr", .value = ENCODER_CFG_NOTRAP_ADDR },
	{ .name = "-seq_jump", .value = ENCODER_CFG_SEQ_JUMP },
	{ .name = "-implicit_return", .value = ENCODER_CFG_IMPLICIT_RETURN },
	{ .name = "-branch_prediction", .value = ENCODER_CFG_BRANCH_PREDICTION },
	{ .name = "-jump-target-cache", .value = ENCODER_CFG_JUMP_TARGET_CACHE },
	{ .name = "-implicit_return_mode", .value = ENCODER_CFG_IMPLICIT_RETURN_MODE },
	{ .name = "-repeated_history", .value = ENCODER_CFG_REPEATED_HISTORY },
	{ .name = "-all_jumps", .value = ENCODER_CFG_ALL_JUMPS },
	{ .name = "-ext_msb", .value = ENCODER_CFG_EXT_MSB },
	{ .name = "-srcid", .value = ENCODER_CFG_SRCID },
	{ .name = "-srcbits", .value = ENCODER_CFG_SRCBITS },
	{ .name = NULL, .value = ENCODER_CFG_INVALID }
};

static struct jim_nvp nvp_inst_mode[] = {
	{ .name = "off", .value = INSTMODE_OFF },
	{ .name = "btm", .value = INSTMODE_BTM },
	{ .name = "htm", .value = INSTMODE_HTM },
	{ .name = NULL, .value = -1 },
};

static struct jim_nvp nvp_sync_mode[] = {
	{ .name = "off", .value = SYNCMODE_OFF },
	{ .name = "messages", .value = SYNCMODE_MESSAGES },
	{ .name = "clock", .value = SYNCMODE_CLOCK },
	{ .name = "instruction", .value = SYNCMODE_INSTRUCTION },
	{ .name = NULL, .value = -1 },
};

static struct jim_nvp nvp_trc_fmt[] = {
	{ .name = "etrace", .value = FORMAT_ETRACE },
	{ .name = "ntrace", .value = FORMAT_NTRACE },
	{ .name = NULL, .value = -1 },
};

static struct jim_nvp nvp_implicit_return_mode[] = {
	{ .name = "no_address", .value = IMPLICITRETURNMODE_NO_ADDRESS },
	{ .name = "partial_address", .value = IMPLICITRETURNMODE_PARTIAL_ADDRESS },
	{ .name = "full_address", .value = IMPLICITRETURNMODE_FULL_ADDRESS },
	{ .name = NULL, .value = -1 },
};

COMMAND_HANDLER(handle_config_command)
{
	if (CMD_ARGC % 2) {
		LOG_ERROR("Command takes an even number of parameters.");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct encoder_info *info = r->encoder_info;

	if (CMD_ARGC == 0) {
		command_print(CMD, "Trace Encoder Base: 0x%" PRIx64, info->base);
		command_print(CMD, "Instruction trace generation mode: %s", jim_nvp_value2name_simple(nvp_inst_mode, info->inst_mode)->name);
		command_print(CMD, "Context: %s", info->context ? "on" : "off");
		command_print(CMD, "Instruction trigger: %s", info->trigger ? "on" : "off");
		command_print(CMD, "Stall:  %s", info->stall  ? "on" : "off");
		command_print(CMD, "Inhibit source field: %s", info->inhb_src ? "on" : "off");
		command_print(CMD, "Instruction trace synchronization mechanism: %s", jim_nvp_value2name_simple(nvp_sync_mode, info->sync_mode)->name);
		command_print(CMD, "The maximum interval of instruction trace synchronization mechanism: %d", info->sync_max);
		command_print(CMD, "Trace recording/protocol format: %s", jim_nvp_value2name_simple(nvp_trc_fmt, info->trc_fmt)->name);
		command_print(CMD, "Full address: %s", info->noaddr_diff ? "on" : "off");
		command_print(CMD, "Sequentially jumps: %s", info->seq_jump ? "on" : "off");
		command_print(CMD, "Implicit return: %s", info->implicit_return ? "on" : "off");
		command_print(CMD, "Branch prediction: %s", info->branch_prediction ? "on" : "off");
		command_print(CMD, "Jump target cache: %s", info->jump_target_cache ? "on" : "off");
		command_print(CMD, "Implicit return mode: %d", info->implicit_return_mode);
		command_print(CMD, "Repeated history: %s", info->repeated_history ? "on" : "off");
		command_print(CMD, "All jumps: %s", info->all_jumps ? "on" : "off");
		command_print(CMD, "Extend MSB address bits: %s", info->ext_msb ? "on" : "off");
		command_print(CMD, "Trace source ID: %d", info->srcid);
		command_print(CMD, "The number of bits in the trace source field: %d", info->srcbits);

		return ERROR_OK;
	}

	struct jim_nvp *n;
	for (unsigned int i = 0; i < CMD_ARGC - 1; i += 2) {
		n = jim_nvp_name2value_simple(nvp_config_opts, CMD_ARGV[i]);
		switch (n->value) {
		case ENCODER_CFG_BASE:
			COMMAND_PARSE_ADDRESS(CMD_ARGV[i + 1], info->base);
			break;
		case ENCODER_CFG_INST_MODE:
			if (!jim_nvp_name2value_simple(nvp_inst_mode, CMD_ARGV[i + 1])->name)
				return ERROR_COMMAND_SYNTAX_ERROR;
			info->inst_mode = jim_nvp_name2value_simple(nvp_inst_mode, CMD_ARGV[i + 1])->value;
			break;
		case ENCODER_CFG_CONTEXT:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->context);
			break;
		case ENCODER_CFG_TRIGGER:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->trigger);
			break;
		case ENCODER_CFG_STALL:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->stall);
			break;
		case ENCODER_CFG_INHB_SRC:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->inhb_src);
			break;
		case ENCODER_CFG_SYNC_MODE:
			if (!jim_nvp_name2value_simple(nvp_sync_mode, CMD_ARGV[i + 1])->name)
				return ERROR_COMMAND_SYNTAX_ERROR;
			info->sync_mode = jim_nvp_name2value_simple(nvp_sync_mode, CMD_ARGV[i + 1])->value;
			break;
		case ENCODER_CFG_SYNC_MAX:
			COMMAND_PARSE_NUMBER(u32, CMD_ARGV[i + 1], info->sync_max);
			break;
		case ENCODER_CFG_TRC_FMT:
			if (!jim_nvp_name2value_simple(nvp_trc_fmt, CMD_ARGV[i + 1])->name)
				return ERROR_COMMAND_SYNTAX_ERROR;
			info->trc_fmt = jim_nvp_name2value_simple(nvp_trc_fmt, CMD_ARGV[i + 1])->value;
			break;
		case ENCODER_CFG_NOADDR_DIFF:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->noaddr_diff);
			break;
		case ENCODER_CFG_NOTRAP_ADDR:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->notrap_addr);
			break;
		case ENCODER_CFG_SEQ_JUMP:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->seq_jump);
			break;
		case ENCODER_CFG_IMPLICIT_RETURN:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->implicit_return);
			break;
		case ENCODER_CFG_BRANCH_PREDICTION:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->branch_prediction);
			break;
		case ENCODER_CFG_JUMP_TARGET_CACHE:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->jump_target_cache);
			break;
		case ENCODER_CFG_IMPLICIT_RETURN_MODE:
			if (!jim_nvp_name2value_simple(nvp_implicit_return_mode, CMD_ARGV[i + 1])->name)
				return ERROR_COMMAND_SYNTAX_ERROR;
			info->implicit_return_mode = jim_nvp_name2value_simple(nvp_implicit_return_mode, CMD_ARGV[i + 1])->value;
			break;
		case ENCODER_CFG_REPEATED_HISTORY:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->repeated_history);
			break;
		case ENCODER_CFG_ALL_JUMPS:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->all_jumps);
			break;
		case ENCODER_CFG_EXT_MSB:
			COMMAND_PARSE_ON_OFF(CMD_ARGV[i + 1], info->ext_msb);
			break;
		case ENCODER_CFG_SRCID:
			COMMAND_PARSE_NUMBER(u32, CMD_ARGV[i + 1], info->srcid);
			break;
		case ENCODER_CFG_SRCBITS:
			COMMAND_PARSE_NUMBER(u32, CMD_ARGV[i + 1], info->srcbits);
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
	struct encoder_info *info = r->encoder_info;

	uint32_t impl, control, feature;
	if (trace_reg_read(target, TE_IMPL + info->base, &impl) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_read(target, TE_CONTROL + info->base, &control) != ERROR_OK)
		return ERROR_FAIL;

	if (trace_reg_read(target, TE_INST_FEATURES + info->base, &feature) != ERROR_OK)
		return ERROR_FAIL;


	command_print(CMD, "trace-encoder @0x%" PRIx64, info->base);
	command_print(CMD, "    impl=0x%x", impl);
	command_print(CMD, "    control=0x%x", control);
	command_print(CMD, "    feature=0x%x", feature);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_enable_command)
{
	struct target *target = get_current_target(CMD_CTX);
        RISCV_INFO(r);
        struct encoder_info *info = r->encoder_info;

	if (!info->was_enabled && encoder_enable(target) != ERROR_OK) {
		LOG_TARGET_ERROR(target, "Failed to enable Trace Encoder.");
		return ERROR_FAIL;
	}
	LOG_TARGET_INFO(target, "Trace Encoder successfully enable.");
	return ERROR_OK;
}

COMMAND_HANDLER(handle_disable_command)
{
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct encoder_info *info = r->encoder_info;

	if (!info->was_enabled) {
		LOG_TARGET_INFO(target, "Trace Encoder already disable.");
		return ERROR_OK;
	}

	if (encoder_disable(target) != ERROR_OK) {
		LOG_TARGET_ERROR(target, "Failed to disable Trace Encoder.");
		return ERROR_FAIL;
	}
	LOG_TARGET_INFO(target, "Trace Encoder successfully disable.");
	return ERROR_OK;
}

COMMAND_HANDLER(handle_start_command)
{
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct encoder_info *info = r->encoder_info;

	if (!info->was_enabled && encoder_enable(target) != ERROR_OK) {
		LOG_TARGET_ERROR(target, "Failed to enable Trace Encoder.");
		return ERROR_FAIL;
	}

	uint32_t control;
	if (trace_reg_read(target, TE_CONTROL + info->base, &control) != ERROR_OK)
		return ERROR_FAIL;
	control = set_field32(control, TE_CONTROL_INSTTRACING, TE_CONTROL_INSTTRACING_ON);

	if (trace_reg_write(target, TE_CONTROL + info->base, control) != ERROR_OK)
		return ERROR_FAIL;

	LOG_TARGET_INFO(target, "Instruction trace is starting to generate.");
	return ERROR_OK;
}

COMMAND_HANDLER(handle_stop_command)
{
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	struct encoder_info *info = r->encoder_info;

	if (!info->was_enabled)
		return ERROR_OK;

	uint32_t control;
	if (trace_reg_read(target, TE_CONTROL + info->base, &control) != ERROR_OK)
		return ERROR_FAIL;
	control = set_field32(control, TE_CONTROL_INSTTRACING, TE_CONTROL_INSTTRACING_OFF);

	if (trace_reg_write(target, TE_CONTROL + info->base, control) != ERROR_OK)
		return ERROR_FAIL;

	LOG_TARGET_INFO(target, "Instruction trace stopped generating.");
	return ERROR_OK;
}

COMMAND_HANDLER(handle_close_command)
{
	struct target *target = get_current_target(CMD_CTX);

	return deactivate_encoder(target);
}

const struct command_registration trace_encoder_command_handlers[] = {
	{
		.name = "config",
		.handler = handle_config_command,
		.mode = COMMAND_ANY,
		.help = "Configuration trace encoder.",
		.usage = "trace_encoder_attribute ...",
	},
	{
		.name = "info",
		.handler = handle_info_command,
		.mode = COMMAND_ANY,
		.help = "display trace encoder info.",
		.usage = "",
	},
	{
		.name = "enable",
		.handler = handle_enable_command,
		.mode = COMMAND_EXEC,
		.help = "enable trace encoder",
		.usage = "",
	},
	{
		.name = "disable",
		.handler = handle_disable_command,
		.mode = COMMAND_EXEC,
		.help = "disable trace encoder",
		.usage = "",
	},
	{
		.name = "start",
		.handler = handle_start_command,
		.mode = COMMAND_EXEC,
		.help = "start trace encoder tracing",
		.usage = "",
	},
	{
		.name = "stop",
		.handler = handle_stop_command,
		.mode = COMMAND_EXEC,
		.help = "stop trace encoder tracing",
		.usage = "",
	},
	{
		.name = "close",
		.handler = handle_close_command,
		.mode = COMMAND_EXEC,
		.help = "close trace encoder hardware",
		.usage = "",
	},
	COMMAND_REGISTRATION_DONE
};

const struct command_registration trace_encoder_command_group_handlers[] = {
       {
               .name = "trace-encoder",
               .mode = COMMAND_ANY,
               .help = "Trace Encoder command group",
               .usage = "",
               .chain = trace_encoder_command_handlers,
       },
       COMMAND_REGISTRATION_DONE
};
