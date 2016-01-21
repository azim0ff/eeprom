#define EEPROM_BASE 						0x2E000
#define EEPROM_BYTES_PER_PAGE 	4096
#define EEPROM_NUM_PAGES 				2
//Keep number of slots low, if not using them. Affects performance
#define EEPROM_NUM_SLOTS				16

typedef struct {
	uint16_t id;
	uint16_t data;
} eeprom_entry_t;

typedef struct {
	eeprom_page_status_t page_status;
} eeprom_page_header;

//there is also module status.. can be done as flags
//or like a last error type deal

typedef enum {
	EEPROM_PAGE_STATUS_VALID 					= 0x00000000,
	//EEPROM_PAGE_STATUS_COPYINPROG 		= 0xAAAAAAAA,	//may not need
	EEPROM_PAGE_STATUS_ERASED 				= 0xFFFFFFFF,
	//available/current/expired
} eeprom_page_status_t;

enum {
	EEPROM_SLOT_MAX_ID 				= EEPROM_NUM_SLOTS-1, //upper limit
	EEPROM_SLOT_EMPTY_ID 			= 0xFFFF,							 //reserved
};

//this is made to match the SDK flash types, but can
//also contain additional values, specific to this module
typedef enum {
	EEPROM_STATUS_OK = SPI_FLASH_RESULT_OK,
	EEPROM_STATUS_ERR = SPI_FLASH_RESULT_ERR,
	EEPROM_STATUS_TIMEOUT = SPI_FLASH_RESULT_TIMEOUT,
	EEPROM_STATUS_ID_NOT_FOUND,
//	EEPROM_STATUS_COMPLETELY_FULL,
} eeprom_status_t;
