// SPDX-License-Identifier: GPL-2.0-or-later

#include "trace.h"
#include "target/target.h"


int trace_reg_read(struct target *target, target_addr_t address, uint32_t *value)
{
        int result = target_read_u32(target, address, value);
        if (result != ERROR_OK)
                LOG_TARGET_ERROR(target, "Failed to read register (addr=0x%" TARGET_PRIxADDR ")", address);
        return result;
}

int trace_reg_write(struct target *target, target_addr_t address, uint32_t value)
{
	int result = target_write_u32(target, address, value);
	if (result != ERROR_OK)
		LOG_TARGET_ERROR(target, "Failed to write register (addr=0x%" TARGET_PRIxADDR ")", address);
	return result;
}
