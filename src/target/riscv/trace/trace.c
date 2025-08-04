// SPDX-License-Identifier: GPL-2.0-or-later

#include "trace.h"
#include "target/target.h"

static int target_read_phys_u32(struct target *target, target_addr_t address, uint32_t *value)
{
	uint8_t value_buf[4];
	if (!target_was_examined(target)) {
		LOG_ERROR("Target not examined yet");
		return ERROR_FAIL;
	}

	int retval = target_read_phys_memory(target, address, 4, 1, value_buf);

	if (retval == ERROR_OK) {
		*value = target_buffer_get_u32(target, value_buf);
		LOG_DEBUG("address: " TARGET_ADDR_FMT ", value: 0x%8.8" PRIx32 "",
				  address,
				  *value);
	} else {
		*value = 0x0;
		LOG_DEBUG("address: " TARGET_ADDR_FMT " failed",
				  address);
	}

	return retval;
}

int trace_reg_read(struct target *target, target_addr_t address, uint32_t *value)
{
        int result = target_read_phys_u32(target, address, value);
        if (result != ERROR_OK)
                LOG_TARGET_ERROR(target, "Failed to read register (addr=0x%" TARGET_PRIxADDR ")", address);
        return result;
}

int trace_reg_write(struct target *target, target_addr_t address, uint32_t value)
{
	int result = target_write_phys_u32(target, address, value);
	if (result != ERROR_OK)
		LOG_TARGET_ERROR(target, "Failed to write register (addr=0x%" TARGET_PRIxADDR ")", address);
	return result;
}
