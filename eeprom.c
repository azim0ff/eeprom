#include "eeprom.h"

// check self consistency, recover as best you can.
// run on boot.
eeprom_status_t
eeprom_init()
{
	//find active page, deal with none/1/2/3+
	uint32_t num_current_pages = 0;
	for (uint32_t i = 0; i < EEPROM_NUM_PAGES; i++) {
		if (eeprom_get_page_status(i) == EEPROM_PAGE_STATUS_VALID) {
			num_current_pages++;
		}
	}

	if (num_current_pages == 0) {
		// uninitialized, reformat?
		return eeprom_format();
	} else if (num_current_pages == 1) {
		// normal, nothing to do
		return EEPROM_STATUS_OK;
	} else if (num_current_pages == 2) {
		// died during packing
		return eeprom_pack();
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
	for (uitn32_t i = EEPROM_BYTES_PER_PAGE - sizeof(eeprom_entry_t); i > 0; i -= sizeof(eeprom_entry_t)) {

	  //if found return value
	  eeprom_entry_t entry;
		SpiFlashOpResult result =
			spi_flash_read(EEPROM_BASE
										+ active_page*EEPROM_BYTES_PER_PAGE
										+ i, 
										&entry, sizeof(entry));
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

	//read last slot to check for room
	eeprom_entry_t entry;
	SpiFlashOpResult result =
			spi_flash_read(EEPROM_BASE
				           + active_page*EEPROM_BYTES_PER_PAGE 
				           + EEPROM_BYTES_PER_PAGE - sizeof(eeprom_entry_t),
				           &entry, sizeof(entry));
	if (result != SPI_FLASH_RESULT_OK) {
		return result;
	}

	//if full, call pack and update active page
	if (entry.id != EEPROM_SLOT_EMPTY_ID) {
		status = eeprom_pack(active_page);
		if (status != EEPROM_STATUS_OK) {
			return status;
		}

		active_page = (active_page + 1) % EEPROM_NUM_PAGES;
	}

	//find last slot in active page
	uint32_t used = EEPROM_BYTES_PER_PAGE - 2*sizeof(eeprom_entry_t)
	for (; used > 0; used -= sizeof(eeprom_entry_t)) {
	  SpiFlashOpResult result =
			spi_flash_read(EEPROM_BASE
										+ active_page*EEPROM_BYTES_PER_PAGE
										+ used, 
										&entry, sizeof(entry));
		if (result != SPI_FLASH_RESULT_OK) {
			return result;
		}
		if (entry.id != EEPROM_SLOT_EMPTY_ID) {
			break;
		}
	}

	//used contains last used slot. write into the next one
	entry.id = id;
	entry.data = data;
	return spi_flash_write(EEPROM_BASE
											+ active_page*EEPROM_BYTES_PER_PAGE
											+ used + sizeof(eeprom_entry_t), (uint32_t*) &entry, sizeof(entry));

}

// ------------------------------------------

static eeprom_status_t
eeprom_erase_page(uint32_t page_index) {
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
eeprom_pack(uint32_t active_page) {

	//find next page
	uint32_t next_page;
	eeprom_status_t status = 
		eeprom_find_next_page(active_page, &next_page);
	if (status != EEPROM_STATUS_OK) {
		return status;
	}

	//make sure next page is indeed empty
	eeprom_page_header h;
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

	//mark new page as valid
	uint32_t valid_status = EEPROM_PAGE_STATUS_VALID;
	result = spi_flash_write(EEPROM_BASE
													+ EEPROM_BYTES_PER_PAGE*next_page
													+ 0, &valid_status, sizeof(valid_status));
	if (result != SPI_FLASH_RESULT_OK) {
		return result;
	}

	//for every possible slot id ...
	for (uint16_t i = 0; i < EEPROM_NUM_SLOTS; i++) {
		//try to read it from old page
		uint16_t data;
		status = eeprom_read(i, &data);
		if (status == EEPROM_STATUS_ID_NOT_FOUND) {
			continue;
		} else if (status != EEPROM_STATUS_OK) {
			return status;
		}

		//if found, write to new page
		status = eeprom_write(i, data);
		if (status != EEPROM_STATUS_OK) {
			return status;
		}
	}

	//make sure there is some empty room after packing
	//find active page
	uint32_t active_page;
	eeprom_status_t status = eeprom_find_active_page(&active_page);
	if (status != EEPROM_STATUS_OK) {
		return status;
	}
	//read last slot to check for room
	eeprom_entry_t entry;
	SpiFlashOpResult result =
			spi_flash_read(EEPROM_BASE
				           + active_page*EEPROM_BYTES_PER_PAGE 
				           + EEPROM_BYTES_PER_PAGE - sizeof(eeprom_entry_t),
				           &entry, sizeof(entry));
	if (result != SPI_FLASH_RESULT_OK) {
		return result;
	}
	//if full -> fatal error
	if (entry.id != EEPROM_SLOT_EMPTY_ID) {
		return EEPROM_STATUS_ERR;
	}

	//erase old page
	return eeprom_erase_page(active_page);
}

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

	//roll forward looking for a status erased. even though
	//this will always be the next page... it's just that
	//maybe somehow it is not erased?

	return ((current_page + 1) % EEPROM_NUM_PAGES);
}