// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef OPENOCD_TARGET_RISCV_TRACE_TRACE_H
#define OPENOCD_TARGET_RISCV_TRACE_TRACE_H

#include "../riscv.h"

int trace_reg_read(struct target *target, target_addr_t address, uint32_t *value);
int trace_reg_write(struct target *target, target_addr_t address, uint32_t value);

enum inst_mode {
	INSTMODE_OFF = 0,
	INSTMODE_BTM = 3,
	INSTMODE_HTM = 6
};

enum sync_mode {
	SYNCMODE_OFF,
	SYNCMODE_MESSAGES,
	SYNCMODE_CLOCK,
	SYNCMODE_INSTRUCTION
};

enum trc_fmt {
	FORMAT_ETRACE,
	FORMAT_NTRACE
};

enum implicit_return_mode {
	IMPLICITRETURNMODE_NO_ADDRESS,
	IMPLICITRETURNMODE_PARTIAL_ADDRESS,
	IMPLICITRETURNMODE_FULL_ADDRESS
};

struct encoder_info {
	target_addr_t base;
	bool was_reset;
	bool was_enabled;
	enum inst_mode inst_mode;
	bool context;
	bool trigger;
	bool stall;
	bool inhb_src;
	enum sync_mode sync_mode;
	unsigned int sync_max;
	enum trc_fmt trc_fmt;
	bool noaddr_diff;
	bool notrap_addr;
	bool seq_jump;
	bool implicit_return;
	bool branch_prediction;
	bool jump_target_cache;
	enum implicit_return_mode implicit_return_mode;
	bool repeated_history;
	bool all_jumps;
	bool ext_msb;
	unsigned int srcid;
	unsigned int srcbits;
};

enum ts_mode {
	NO_MODE,
	EXTERNAL,
	INTERNAL_SYSTEM,
	INTERNAL_CORE,
	SHARED
};

struct timestamp_info {
	target_addr_t base;
	bool impl_reset;
	bool was_reset;
	bool was_enabled;
	bool run_halt;
	enum ts_mode mode;
	unsigned int prescale;
};

struct funnel_info {
	target_addr_t base;
	bool was_reset;
	bool was_enabled;
	unsigned int port;
};

struct atbbridge_info {
	target_addr_t base;
	bool was_reset;
	bool was_enabled;
	unsigned int id;
};

struct cs_funnel_info {
	target_addr_t base;
	bool was_enabled;
	unsigned int port;
	unsigned int priority;
};

extern const struct command_registration trace_encoder_command_group_handlers[];

extern const struct command_registration timestamp_command_group_handlers[];

extern const struct command_registration trace_funnel_command_group_handlers[];

extern const struct command_registration atbbridge_command_group_handlers[];

extern const struct command_registration coresight_funnel_command_group_handlers[];

#endif
