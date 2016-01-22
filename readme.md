This is an implementation of a slot-based EEPROM for ESP8266. It should work with both the non-OS and the RTOS SDKs. It has no additional dependencies aside from the SDK.

*How to use*
Add to project and edit eeprom.h. At the top of the file you will see several settings that should be selected to match your system. Let''s go over them.

#define EEPROM_BASE 						0x2E000
#define EEPROM_BYTES_PER_PAGE 	4096
#define EEPROM_NUM_PAGES 				2
#define EEPROM_NUM_SLOTS				16

EEPROM_BASE is the *FLASH* (not RAM) address of the EEPROM memory. Make sure you don''t put some other code or data into this space. Flash page size (minimum erasable unit) is set by EEPROM_BYTES_PER_PAGE. Each page is typically 4096 bytes long, but it may differ. Check your flash data sheet. EEPROM_NUM_PAGES represents the number of flash pages that are given to the EEPROM. Minimum is 2, but may be more. The more pages you give to EEPROM the more write endurance it will have, at the expense of space. Again, be careful not to overlap this space with some other data. EEPROM_NUM_SLOTS represents the number of EEPROM slots available for use. Theoretically there are up to 0xFFFF slots (0x0..0xFFFE), but you should keep this value to the minimum that is actually used. High number of slots will slow down the 'pack' operation, so only set it as high as you will actually use.

In your code, call eeprom_init() from user_init() and make sure there are no errors. After this, read and write to EEPROM like this:

//read
uint16_t key = 123;
uint16_t value;
eeprom_status status = eeprom_read(key, &value);
if (status != EEPROM_STATUS_OK) {
  //handle error
}

//write
key = 456;
value = 0xBABE;
status = eeprom_write(key, value);
if (status != EEPROM_STATUS_OK) {
  //handle error
}
