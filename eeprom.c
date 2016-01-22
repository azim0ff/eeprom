#include "eeprom.h"

#include <stdio.h>
#define EEPROM_DEBUG_PRINTF(...) printf(__VA_ARGS__)

// iterate over pages to find the active one
static eeprom_status_t
eeprom_find_active_page(uint32_t* page_index) {

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

		if (t.page_status == EEPROM_PAGE_STATUS_VALID) {
			*page_index = i;
			return EEPROM_STATUS_OK;
		}
	}

	//did not find
	return EEPROM_STATUS_ERR;
}

static eeprom_status_t
eeprom_find_next_page(uint32_t current_page, uint32_t* next_page) {

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
	if (t.page_status != EEPROM_PAGE_STATUS_VALID) {
		return EEPROM_STATUS_ERR;
	}
	EEPROM_DEBUG_PRINTF("current page is current\n");

	//roll forward looking for a status erased. even though
	//this will always be the next page... it's just that
	//maybe somehow it is not erased?

	*next_page = ((current_page + 1) % EEPROM_NUM_PAGES);
	return EEPROM_STATUS_OK;
}

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
	uint32_t t = EEPROM_PAGE_STATUS_VALID;
	return spi_flash_write(EEPROM_BASE
												+ EEPROM_BYTES_PER_PAGE*0
												+ 0, &t, sizeof(t));
}


static eeprom_status_t
//IRAM_ATTR
eeprom_pack(uint32_t active_page) {
	EEPROM_DEBUG_PRINTF("packing...\n");

	//find next page
	uint32_t next_page;
	eeprom_status_t status = 
		eeprom_find_next_page(active_page, &next_page);
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

	//mark new page as valid
	uint32_t valid_status = EEPROM_PAGE_STATUS_VALID;
	result = spi_flash_write(EEPROM_BASE
													+ EEPROM_BYTES_PER_PAGE*next_page
													+ 0, &valid_status, sizeof(valid_status));
	if (result != SPI_FLASH_RESULT_OK) {
		return result;
	}

	//for every possible slot id ...
	uint32_t num_written = 0;
	for (uint16_t i = 0; i < EEPROM_NUM_SLOTS; i++) {

//**** eeprom_read will look for active page, which has just been reset

		//try to read it from old page
		uint16_t data;
		status = eeprom_read(i, &data);
		if (status == EEPROM_STATUS_ID_NOT_FOUND) {
			continue;
		} else if (status != EEPROM_STATUS_OK) {
			return status;
		}
		EEPROM_DEBUG_PRINTF("read [%u,0x%04x]\n", i, data);

		//if found, write to new page
		eeprom_entry_t entry;
		entry.id = i;
		entry.data = data;
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
	if (entry.id != EEPROM_SLOT_EMPTY_ID) {
		return EEPROM_STATUS_ERR;
	}
	EEPROM_DEBUG_PRINTF("packed page not full - good!\n");

	//erase old page
	return eeprom_erase_page(active_page);
}

// -----------------------------------------

// check self consistency, recover as best you can.
// run on boot.
eeprom_status_t
eeprom_init()
{
	//find active page, deal with none/1/2/3+
	uint32_t num_current_pages = 0;
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

		if (t == EEPROM_PAGE_STATUS_VALID) {
			num_current_pages++;
		}
	}
	EEPROM_DEBUG_PRINTF("found %d active pages\n", num_current_pages);

	if (num_current_pages == 0) {
		// uninitialized, reformat?
		return eeprom_format();
	} else if (num_current_pages == 1) {
		// normal, nothing to do
		return EEPROM_STATUS_OK;
	} else if (num_current_pages == 2) {
		// died during packing
		uint32_t first_current_page;
		eeprom_status_t status = eeprom_find_active_page(&first_current_page);
		if (status != EEPROM_STATUS_OK) {
			return status;
		}

		return eeprom_pack(first_current_page);
	} else {
		// unknown corruption, reformat?
		return eeprom_format();
	}
}

eeprom_status_t
eeprom_read(uint16_t id, uint16_t* dest) {
	//check id
	if (id > EEPROM_SLOT_MAX_ID || id == EEPROM_SLOT_EMPTY_ID) {
		return EEPROM_STATUS_ERR;
	}

	//find active page
	uint32_t active_page;
	eeprom_status_t status = eeprom_find_active_page(&active_page);
	if (status != EEPROM_STATUS_OK) {
		return status;
	}

	//starting from back, look for id
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

		if (entry.id == id) {
			*dest = entry.data;
			return EEPROM_STATUS_OK;
		}
	}

	//return not found
	return EEPROM_STATUS_ID_NOT_FOUND;
}

eeprom_status_t
eeprom_write(uint16_t id, uint16_t data) {
	EEPROM_DEBUG_PRINTF("writing %u 0x%04x\n", id, data);
	//check id	
	if (id > EEPROM_SLOT_MAX_ID || id == EEPROM_SLOT_EMPTY_ID) {
		return EEPROM_STATUS_ERR;
	}

	//find active page
	uint32_t active_page;
	eeprom_status_t status = eeprom_find_active_page(&active_page);
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
	EEPROM_DEBUG_PRINTF("last slot: id: %u, data: 0x%04x\n", entry.id, entry.data);

	//if full, call pack and update active page
	if (entry.id != EEPROM_SLOT_EMPTY_ID) {
		status = eeprom_pack(active_page);
		if (status != EEPROM_STATUS_OK) {
			return status;
		}

		active_page = (active_page + 1) % EEPROM_NUM_PAGES;
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
		if (entry.id != EEPROM_SLOT_EMPTY_ID) {
			break;
		}
	}
	EEPROM_DEBUG_PRINTF("last written page offset: %u bytes\n", used);

	//used contains last used slot. write into the next one
	entry.id = id;
	entry.data = data;
	return spi_flash_write(EEPROM_BASE
											+ active_page*EEPROM_BYTES_PER_PAGE
											+ used + sizeof(eeprom_entry_t), (uint32_t*) &entry, sizeof(entry));

}