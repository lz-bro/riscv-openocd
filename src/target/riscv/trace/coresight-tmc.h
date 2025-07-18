// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef OPENOCD_TARGET_RISCV_TRACE_CORESIGHT_TMC_H
#define OPENOCD_TARGET_RISCV_TRACE_CORESIGHT_TMC_H

#include "target/target.h"

#define TMC_RSZ			0x004
#define TMC_STS			0x00c
#define TMC_RRD			0x010
#define TMC_RRP			0x014
#define TMC_RWP			0x018
#define TMC_TRG			0x01c
#define TMC_CTL			0x020
#define TMC_RWD			0x024
#define TMC_MODE		0x028
#define TMC_LBUFLEVEL		0x02c
#define TMC_CBUFLEVEL		0x030
#define TMC_BUFWM		0x034
#define TMC_RRPHI		0x038
#define TMC_RWPHI		0x03c
#define TMC_AXICTL		0x110
#define TMC_DBALO		0x118
#define TMC_DBAHI		0x11c
#define TMC_FFSR		0x300
#define TMC_FFCR		0x304
#define TMC_PSCR		0x308
#define TMC_ITMISCOP0		0xee0
#define TMC_ITTRFLIN		0xee8
#define TMC_ITATBDATA0		0xeec
#define TMC_ITATBCTR2		0xef0
#define TMC_ITATBCTR1		0xef4
#define TMC_ITATBCTR0		0xef8
#define TMC_LAR			0xfb0
#define TMC_AUTHSTATUS		0xfb8

/* register description */
/* TMC_CTL - 0x020 */
#define TMC_CTL_CAPT_EN		1ULL
/* TMC_STS - 0x00C */
#define TMC_STS_FULL		1ULL
#define TMC_STS_TRIGGERED	2ULL
#define TMC_STS_TMCREADY	4ULL
#define TMC_STS_MEMERR		0x20ULL
/* TMC_FFCR - 0x304 */
#define TMC_FFCR_EN_FMT		1ULL
#define TMC_FFCR_EN_TI		2ULL
#define TMC_FFCR_FON_FLIN	0x10ULL
#define TMC_FFCR_FON_TRIG_EVT	0x20ULL
#define TMC_FFCR_FLUSHMAN	0x40ULL
#define TMC_FFCR_TRIGON_TRIGIN	0x100ULL
#define TMC_FFCR_STOP_ON_FLUSH	0x1000ULL

/* TMC_LAR - 0xfb0 */
#define TMC_UNLOCK		0xc5acce55

enum tmc_type {
	TMC_TYPE_ETB,
	TMC_TYPE_ETF,
	TMC_TYPE_ETR,
};

enum tmc_mode {
	TMC_MODE_CIRCULAR_BUFFER,
	TMC_MODE_SOFTWARE_FIFO,
	TMC_MODE_HARDWARE_FIFO,
};

enum coresight_tmc_cfg_opts {
	CS_TMC_CFG_ETF_BASE,
	CS_TMC_CFG_ETR_BASE,
	CS_TMC_CFG_HWADDR,
	CS_TMC_CFG_SIZE,
	CS_TMC_CFG_INVALID = -1
};

/* Generic functions */
int tmc_read_rrp(struct target *target, target_addr_t tmc_base, uint64_t *rrp);
int tmc_read_rwp(struct target *target, target_addr_t tmc_base, uint64_t *rwp);
int tmc_write_rrp(struct target *target, target_addr_t tmc_base, uint64_t rrp);
int tmc_write_rwp(struct target *target, target_addr_t tmc_base, uint64_t rwp);
int tmc_wait_for_tmcready(struct target *target, target_addr_t tmc_base);
int tmc_flush_and_stop(struct target *target, target_addr_t tmc_base);

/* ETF functions */
int coresight_etf_enable(struct target *target);
int coresight_etf_disable(struct target *target);

/* ETR functions */
int tmc_sync_etr_buf(struct target *target);
int coresight_etr_enable(struct target *target);
int coresight_etr_disable(struct target *target);
#endif
