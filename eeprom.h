#ifndef EEPROM_H
#define	EEPROM_H

#include <stdint.h>
#include "c_types.h"
#include "espressif/spi_flash.h"

#define EEPROM_BASE 						0x2E000
#define EEPROM_BYTES_PER_PAGE 	4096
#define EEPROM_NUM_PAGES 				2
//Keep number of slots low, if not using them. Affects performance
#define EEPROM_NUM_KEYS					16

typedef enum {
	EEPROM_PAGE_STATUS_ACTIVE 				= 0x00000000,
	EEPROM_PAGE_STATUS_COPY						= 0xAAAAAAAA,
	EEPROM_PAGE_STATUS_ERASED 				= 0xFFFFFFFF,
} eeprom_page_status_t;

enum {
	EEPROM_MAX_KEY 				= EEPROM_NUM_KEYS-1,  //upper limit
	EEPROM_EMPTY_KEY 			= 0xFFFF,							//reserved
};

//this is made to match the SDK flash types, but can
//also contain additional values, specific to this module
typedef enum {
	EEPROM_STATUS_OK = SPI_FLASH_RESULT_OK,
	EEPROM_STATUS_ERR = SPI_FLASH_RESULT_ERR,
	EEPROM_STATUS_TIMEOUT = SPI_FLASH_RESULT_TIMEOUT,
	EEPROM_STATUS_NOT_FOUND,
} eeprom_status_t;

typedef struct __attribute__((aligned(4))) {
	uint16_t key;
	uint16_t value;
} eeprom_entry_t;

typedef struct __attribute__((aligned(4))) {
	eeprom_page_status_t page_status;
} eeprom_page_header_t;

eeprom_status_t eeprom_init();
eeprom_status_t eeprom_read(uint16_t key, uint16_t* value);
eeprom_status_t eeprom_write(uint16_t key, uint16_t value);

#endif // EEPROM_H
