#include "BT2Reader.h"
#include "../logging/logger.h"

void BT2Reader::printHex(uint8_t * data, int datalen) { printHex(data, datalen, false); }
void BT2Reader::printHex(uint8_t * data, int datalen, boolean printIndex) {
	#ifdef SERIALLOGGER
	if (printIndex) {
		for (int i = 0; i < datalen; i++) { Serial.printf("%2d%s", i, (i < datalen-1 ? " " : "\n")); }
	}
	for (int i = 0; i < datalen; i++) { Serial.printf("%02X%s", (uint8_t)data[i], (i < datalen-1 ? " " : "\n")); }
	#endif
}

void BT2Reader::printUuid(uint8_t * data, int datalen) {
	#ifdef SERIALLOGGER
	for (int i = datalen - 1; i >= 0; i--) {
		Serial.printf("%02X%s", (uint8_t)data[i], (UUID_DASHES[datalen - i - 1] == 1 ? "-" : ""));
	}
	Serial.println("");
	#endif
}

boolean BT2Reader::getIsReceivedDataValid(uint8_t * data) {
	return (getProvidedModbusChecksum(data) == getCalculatedModbusChecksum(data));
}

uint16_t BT2Reader::getProvidedModbusChecksum(uint8_t * data) {
	int checksumIndex = (int)((data[2]) + 3);
	return (data[checksumIndex] + data[checksumIndex + 1] * 256);
}

uint16_t BT2Reader::getCalculatedModbusChecksum(uint8_t * data) {
	return (getCalculatedModbusChecksum(data, 0, data[2] + 3));
}


uint16_t BT2Reader::getCalculatedModbusChecksum(uint8_t * data, int start, int end) {	
	uint8_t xxor = 0;
	uint16_t crc = 0xFFFF;
	for (int i = start; i < end; i++) {
		xxor = data[i] ^ crc;
		crc >>= 8;
		crc ^= MODBUS_TABLE_A001[xxor & 0xFF];
	}
	return crc;
}



