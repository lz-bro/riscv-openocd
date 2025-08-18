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

static int target_read_phys_buffer_default(struct target *target, target_addr_t address, uint32_t count, uint8_t *buffer)
{
	uint32_t size;
	unsigned int data_bytes = target_data_bits(target) / 8;

	/* Align up to maximum bytes. The loop condition makes sure the next pass
	 * will have something to do with the size we leave to it. */
	for (size = 1;
			size < data_bytes && count >= size * 2 + (address & size);
			size *= 2) {
		if (address & size) {
			int retval = target_read_phys_memory(target, address, size, 1, buffer);
			if (retval != ERROR_OK)
				return retval;
			address += size;
			count -= size;
			buffer += size;
		}
	}

	/* Read the data with as large access size as possible. */
	for (; size > 0; size /= 2) {
		uint32_t aligned = count - count % size;
		if (aligned > 0) {
			int retval = target_read_phys_memory(target, address, size, aligned / size, buffer);
			if (retval != ERROR_OK)
				return retval;
			address += aligned;
			count -= aligned;
			buffer += aligned;
		}
	}

	return ERROR_OK;
}

int target_read_phys_buffer(struct target *target, target_addr_t address, uint32_t size, uint8_t *buffer)
{
	LOG_DEBUG("reading buffer of %" PRIu32 " byte at " TARGET_ADDR_FMT,
			  size, address);

	if (!target_was_examined(target)) {
		LOG_ERROR("Target not examined yet");
		return ERROR_FAIL;
	}

	if (size == 0)
		return ERROR_OK;

	if ((address + size - 1) < address) {
		/* GDB can request this when e.g. PC is 0xfffffffc */
		LOG_ERROR("address + size wrapped (" TARGET_ADDR_FMT ", 0x%08" PRIx32 ")",
				  address,
				  size);
		return ERROR_FAIL;
	}

	return target_read_phys_buffer_default(target, address, size, buffer);
}
