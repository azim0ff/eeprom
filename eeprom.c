/****************************************************************************
 * Copyright 2015,2016 azim0ff/kacangbawang.com
 *
 * Description: EEPROM emulation for ESP8266
 *
 * License: MIT License
 ****************************************************************************/

#include "eeprom.h"

#if 0
	#include <stdio.h>
	#define EEPROM_DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
	#define EEPROM_DEBUG_PRINTF(...)
#endif

// iterate over pages to find
static eeprom_status_t
eeprom_find_page(eeprom_page_status_t status, uint32_t* found_page) {

	eeprom_page_header_t t;

	// read page status for every page until first active
	for (uint32_t i = 0; i < EEPROM_NUM_PAGES; i++) {
		
		SpiFlashOpResult result =
			spi_flash_read(EEPROM_BASE
									+ EEPROM_BYTES_PER_PAGE*i
									+ 0, (uint32_t*) &t, sizeof(t));
		if (result != SPI_FLASH_RESULT_OK) {
			return result;
		}

		if (t.page_status == status) {
			*found_page = i;
			return EEPROM_STATUS_OK;
		}
	}

	//did not find
	return EEPROM_STATUS_NOT_FOUND;
}

static eeprom_status_t
eeprom_get_pack_dest(uint32_t current_page, uint32_t* next_page) {

	EEPROM_DEBUG_PRINTF("find next page, current: %u\n", current_page);

	if (current_page >= EEPROM_NUM_PAGES) {
		return EEPROM_STATUS_ERR;
	}

	//make sure current is really current
	eeprom_page_header_t t;
	SpiFlashOpResult result = 
		spi_flash_read(EEPROM_BASE
									+ EEPROM_BYTES_PER_PAGE*current_page
									+ 0, (uint32_t*) &t, sizeof(t));
	if (result != SPI_FLASH_RESULT_OK) {
		return result;
	}
	if (t.page_status != EEPROM_PAGE_STATUS_ACTIVE) {
		return EEPROM_STATUS_ERR;
	}
	EEPROM_DEBUG_PRINTF("current page is current\n");

	//roll forward looking for a fitting target.
	//good place to check something about the new page
	//before designating as next active.

	*next_page = ((current_page + 1) % EEPROM_NUM_PAGES);
	return EEPROM_STATUS_OK;
}

//page_index is the local index (eg 0/1)
static eeprom_status_t
eeprom_erase_page(uint16_t page_index) {
	if (page_index >= EEPROM_NUM_PAGES) {
		return EEPROM_STATUS_ERR;
	}

	return spi_flash_erase_sector(EEPROM_BASE/EEPROM_BYTES_PER_PAGE + page_index);
}

// nuke everything, and start from beginning
static eeprom_status_t
eeprom_format() {
	//for each page, erase
	for (uint32_t i = 0; i < EEPROM_NUM_PAGES; i++) {
		eeprom_status_t t = eeprom_erase_page(i);
		if (t != EEPROM_STATUS_OK) {
			return t;
		}
	}

	//first page write header
	uint32_t t = EEPROM_PAGE_STATUS_ACTIVE;
	return spi_flash_write(EEPROM_BASE
												+ EEPROM_BYTES_PER_PAGE*0
												+ 0, &t, sizeof(t));
}


static eeprom_status_t
eeprom_set_page_status(uint32_t page, eeprom_status_t status) {
	
	if (page >= EEPROM_NUM_PAGES) {
		return EEPROM_STATUS_ERR;
	}

	SpiFlashOpResult result = spi_flash_write(EEPROM_BASE
													+ EEPROM_BYTES_PER_PAGE*page
													+ 0, &status, sizeof(status));
	if (result != SPI_FLASH_RESULT_OK) {
		return result;
	}

	return EEPROM_STATUS_OK;
}

static eeprom_status_t
eeprom_pack() {
	EEPROM_DEBUG_PRINTF("packing...\n");

	//find active page
	uint32_t active_page;
	eeprom_status_t status = eeprom_find_page(EEPROM_PAGE_STATUS_ACTIVE, &active_page);
	if (status != EEPROM_STATUS_OK) {
		return status;
	}

	//find next page
	uint32_t next_page = 0;
	status = eeprom_get_pack_dest(active_page, &next_page);
	if (status != EEPROM_STATUS_OK) {
		return status;
	}
	EEPROM_DEBUG_PRINTF("next page: %u\n", next_page);

	//make sure next page is indeed empty
	eeprom_page_header_t h;
	SpiFlashOpResult result =
			spi_flash_read(EEPROM_BASE
										+ EEPROM_BYTES_PER_PAGE*next_page
										+ 0, (uint32_t*) &h, sizeof(h));
	if (result != SPI_FLASH_RESULT_OK) {
		return result;
	}
	if (h.page_status != EEPROM_PAGE_STATUS_ERASED) {
		return EEPROM_STATUS_ERR;
	}
	EEPROM_DEBUG_PRINTF("next page header: %x\n", h.page_status);

	//mark destination
	status = eeprom_set_page_status(next_page, EEPROM_PAGE_STATUS_COPY);
	if (status != EEPROM_STATUS_OK) {
		return status;
	}

	//for every possible slot id ...
	uint32_t num_written = 0;
	for (uint16_t i = 0; i < EEPROM_NUM_KEYS; i++) {

		//try to read it from old page
		uint16_t data;
		status = eeprom_read(i, &data); //will read from active page!
		if (status == EEPROM_STATUS_NOT_FOUND) {
			continue;
		} else if (status != EEPROM_STATUS_OK) {
			return status;
		}
		EEPROM_DEBUG_PRINTF("read [%u,0x%04x]\n", i, data);

		//if found, write to new page
		eeprom_entry_t entry;
		entry.key = i;
		entry.value = data;
		num_written++;
		EEPROM_DEBUG_PRINTF("about to write %u/0x%04x to page: %u, from: 0x%08x to phyaddr: 0x%05x\n", 
													i, data, next_page, &entry, EEPROM_BASE
													+ EEPROM_BYTES_PER_PAGE*next_page
													+ num_written*sizeof(eeprom_entry_t));
		result = spi_flash_write(EEPROM_BASE
													+ EEPROM_BYTES_PER_PAGE*next_page
													+ num_written*sizeof(eeprom_entry_t),
													(uint32_t*) &entry, sizeof(entry));
		if (result != SPI_FLASH_RESULT_OK) {
			EEPROM_DEBUG_PRINTF("result: %d\n", result);
			return result;
		}
		EEPROM_DEBUG_PRINTF("wrote [%u,0x%04x]\n",i,data);
	}

	//make sure there is some empty room after packing
	//read last slot to check for room
	eeprom_entry_t entry;
	result = spi_flash_read(EEPROM_BASE
				           + next_page*EEPROM_BYTES_PER_PAGE 
				           + EEPROM_BYTES_PER_PAGE - sizeof(eeprom_entry_t),
				           (uint32_t*) &entry, sizeof(entry));
	if (result != SPI_FLASH_RESULT_OK) {
		return result;
	}
	EEPROM_DEBUG_PRINTF("after pack space check, last slot: id: %u, data: 0x%04x\n", entry.id, entry.data);
	//if full -> fatal error
	if (entry.key != EEPROM_EMPTY_KEY) {
		return EEPROM_STATUS_ERR;
	}
	EEPROM_DEBUG_PRINTF("packed page not full - good!\n");

	//erase old page
	status = eeprom_erase_page(active_page);
	if (status != EEPROM_STATUS_OK) {
		return status;
	}

	//upgrade new page
	return eeprom_set_page_status(next_page, EEPROM_PAGE_STATUS_ACTIVE);
}

// -----------------------------------------

// check self consistency, recover as best you can.
// run on boot.
eeprom_status_t
eeprom_init()
{
	//count statuses among all pages
	uint32_t num_active = 0;
	uint32_t num_copy = 0;
	for (uint32_t i = 0; i < EEPROM_NUM_PAGES; i++) {

		//read status word
		eeprom_page_status_t t;
		SpiFlashOpResult result =
			spi_flash_read(EEPROM_BASE
									+ EEPROM_BYTES_PER_PAGE*i
									+ 0, (uint32_t*) &t, sizeof(t));
		if (result != SPI_FLASH_RESULT_OK) {
			return result;
		}

		switch(t) {
			case EEPROM_PAGE_STATUS_ACTIVE:
				num_active++;
				break;
			case EEPROM_PAGE_STATUS_COPY:
				num_copy++;
				break;
			default:
				break;
		}
	}
	EEPROM_DEBUG_PRINTF("found %u active, %u copy\n", num_active, num_copy);

	//decision matrix
	eeprom_status_t status;
	if (num_active >= 2) {
		//nuke
		return eeprom_format();
	} else if (num_copy >= 2) {
		//nuke
		return eeprom_format();
	} else if (num_active == 0 && num_copy == 0) {
		//new
		return eeprom_set_page_status(0, EEPROM_PAGE_STATUS_ACTIVE);
	} else if (num_active == 1 && num_copy == 0) {
		//normal
		return EEPROM_STATUS_OK;
	} else if (num_active == 0 && num_copy == 1) {
		//copy complete, but erase possibly not
		//find copy page
		uint32_t copy_page;
		status = eeprom_find_page(EEPROM_PAGE_STATUS_COPY, &copy_page);
		if (status != EEPROM_STATUS_OK) {
			return status;
		}
		//mark as current
		status = eeprom_set_page_status(copy_page, EEPROM_PAGE_STATUS_ACTIVE);
		if (status != EEPROM_STATUS_OK) {
			return status;
		}
		//re-erase all others
		for (uint32_t i = 0; i < EEPROM_NUM_PAGES; i++) {
			if (i == copy_page) {
				continue;
			}
			status = eeprom_erase_page(i);
			if (status != EEPROM_STATUS_OK) {
				return status;
			}
		}
	} else if (num_active == 1 && num_copy == 1) {
		//copy incomplete
		//find pack dest
		uint32_t copy_page;
		status = eeprom_find_page(EEPROM_PAGE_STATUS_COPY, &copy_page);
		if (status != EEPROM_STATUS_OK) {
			return status;
		}
		//erase dest
		status = eeprom_erase_page(copy_page);
		if (status != EEPROM_STATUS_OK) {
			return status;
		}
		//redo pack
		return eeprom_pack();
	}

	//we should never make it to here
	return EEPROM_STATUS_ERR;
}

eeprom_status_t
eeprom_read(uint16_t key, uint16_t* value) {
	//check id
	if (key > EEPROM_MAX_KEY || key == EEPROM_EMPTY_KEY) {
		return EEPROM_STATUS_ERR;
	}

	//find active page
	uint32_t active_page;
	eeprom_status_t status = eeprom_find_page(EEPROM_PAGE_STATUS_ACTIVE, &active_page);
	if (status != EEPROM_STATUS_OK) {
		return status;
	}

	//starting from back, look for key
	for (uint32_t i = EEPROM_BYTES_PER_PAGE - sizeof(eeprom_entry_t); i > 0; i -= sizeof(eeprom_entry_t)) {

	  //if found return value
	  eeprom_entry_t entry;
		SpiFlashOpResult result =
			spi_flash_read(EEPROM_BASE
										+ active_page*EEPROM_BYTES_PER_PAGE
										+ i, 
										(uint32_t*) &entry, sizeof(entry));
		if (result != SPI_FLASH_RESULT_OK) {
			return result;
		}

		if (entry.key == key) {
			*value = entry.value;
			return EEPROM_STATUS_OK;
		}
	}

	//return not found
	return EEPROM_STATUS_NOT_FOUND;
}

eeprom_status_t
eeprom_write(uint16_t key, uint16_t value) {
	EEPROM_DEBUG_PRINTF("writing %u 0x%04x\n", key, value);
	//check key	
	if (key > EEPROM_MAX_KEY || key == EEPROM_EMPTY_KEY) {
		return EEPROM_STATUS_ERR;
	}

	//find active page
	uint32_t active_page;
	eeprom_status_t status = eeprom_find_page(EEPROM_PAGE_STATUS_ACTIVE, &active_page);
	if (status != EEPROM_STATUS_OK) {
		return status;
	}
	EEPROM_DEBUG_PRINTF("activate page: %u\n", active_page);

	//read last slot to check for room
	eeprom_entry_t entry;
	SpiFlashOpResult result =
			spi_flash_read(EEPROM_BASE
				           + active_page*EEPROM_BYTES_PER_PAGE 
				           + EEPROM_BYTES_PER_PAGE - sizeof(eeprom_entry_t),
				           (uint32_t*) &entry, sizeof(entry));
	if (result != SPI_FLASH_RESULT_OK) {
		return result;
	}
	EEPROM_DEBUG_PRINTF("last slot: key: %u, value: 0x%04x\n", entry.key, entry.value);

	//if full, call pack and update active page
	if (entry.key != EEPROM_EMPTY_KEY) {
		status = eeprom_pack();
		if (status != EEPROM_STATUS_OK) {
			return status;
		}

		status = eeprom_find_page(EEPROM_PAGE_STATUS_ACTIVE, &active_page);
		if (status != EEPROM_STATUS_OK) {
			return status;
		}
	}
	EEPROM_DEBUG_PRINTF("active page: %u\n", active_page);

	//find last slot in active page
	uint32_t used = EEPROM_BYTES_PER_PAGE - 2*sizeof(eeprom_entry_t);
	for (; used > 0; used -= sizeof(eeprom_entry_t)) {
	  SpiFlashOpResult result =
			spi_flash_read(EEPROM_BASE
										+ active_page*EEPROM_BYTES_PER_PAGE
										+ used, 
										(uint32_t*) &entry, sizeof(entry));
		if (result != SPI_FLASH_RESULT_OK) {
			return result;
		}
		if (entry.key != EEPROM_EMPTY_KEY) {
			break;
		}
	}
	EEPROM_DEBUG_PRINTF("last written page offset: %u bytes\n", used);

	//used contains last used slot. write into the next one
	entry.key = key;
	entry.value = value;
	return spi_flash_write(EEPROM_BASE
											+ active_page*EEPROM_BYTES_PER_PAGE
											+ used + sizeof(eeprom_entry_t), (uint32_t*) &entry, sizeof(entry));

}
