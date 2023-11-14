#include "BT2Reader.h"

/** Prints bms data received.  Just follow the logger.log code to reconstruct any voltages, temperatures etc.
 *  https://www.dropbox.com/s/03vfqklw97hziqr/%E9%80%9A%E7%94%A8%E5%8D%8F%E8%AE%AE%20V2%20%28%E6%94%AF%E6%8C%8130%E4%B8%B2%29%28Engrish%29.xlsx?dl=0
 *	^^^ has details on the data formats
 */

int BT2Reader::printRegister(uint16_t registerAddress) {

	#ifndef SERIALLOGGER
	return;
	#endif

	int registerDescriptionIndex = getRegisterDescriptionIndex(registerAddress);
	int registerValueIndex = getRegisterValueIndex(registerAddress);
	if (registerDescriptionIndex == -1) {
		//logger.log(INFO,"printRegister: invalid register address 0x%04X; not found in description table; aborting\n", registerAddress);
		return (1);
	}
	if (registerValueIndex == -1) {
		//logger.log(INFO,"printRegister: invalid register address 0x%04X; not found in values table; aborting\n", registerAddress);
		return (1);
	}

	

	uint16_t registerValue = registerValues[registerValueIndex].value;
	uint8_t msb = (registerValue >> 8) & 0xFF;
	uint8_t lsb = (registerValue) & 0xFF;	

	const REGISTER_DESCRIPTION * rr = &registerDescription[registerDescriptionIndex];
	logger.log(INFO,"BT2Reader:",false);	
	logger.log(INFO,"%10s: ", peripheryName);
	logger.log(INFO,"%35s (%04X): ", rr->name, rr->address);

	switch(rr->type) {
		case RENOGY_BYTES: 
			{
				for (int i = 0; i < rr->bytesUsed / 2; i++) {
					Serial.printf("%02X ", (registerValues[registerValueIndex + i].value / 256) &0xFF);
					Serial.printf("%02X ", (registerValues[registerValueIndex + i].value) &0xFF);
				}
				break;
			}
		
		case RENOGY_CHARS: 
			{
				for (int i = 0; i < rr->bytesUsed / 2; i++) {
					Serial.printf("%c", (char)((registerValues[registerValueIndex + i].value / 256) &0xFF));
					Serial.printf("%c", (char)((registerValues[registerValueIndex + i].value) &0xFF));
				}
				break;
			}
		
		case RENOGY_DECIMAL: logger.log(INFO,"%5d", registerValue); break;
		case RENOGY_VOLTS: logger.log(INFO,"%5.1f Volts", (float)(registerValue) * rr->multiplier); break;
		case RENOGY_AMPS: logger.log(INFO,"%5.2f Amps", (float)(registerValue) * rr->multiplier); break;
		case RENOGY_AMP_HOURS: logger.log(INFO,"%3d AH", registerValue); break;
		case RENOGY_COEFFICIENT: logger.log(INFO,"%5.3f mV/℃/2V", (float)(registerValue) * rr->multiplier); break;
		case RENOGY_TEMPERATURE:
			{
				Serial.print((lsb & 0x80) > 0 ? '-' : '+',false);
				Serial.printf("%d C, ", lsb & 0x7F);
				Serial.print((msb & 0x80) > 0 ? '-' : '+',false);
				Serial.printf("%d C, ", msb & 0x7F); 
				break;
			}
		case RENOGY_OPTIONS:
			{
				Serial.printf("Option %d (", registerValue);
				int renogyOptionsSize = sizeof(renogyOptions) / sizeof(renogyOptions[0]);
				for (int i = 0; i < renogyOptionsSize; i++) {
					if (renogyOptions[i].registerAddress == registerAddress && registerValue == renogyOptions[i].option) {
						Serial.printf("%s)",renogyOptions[i].optionName);
						break;
					}
				}
				break;
			}
		
		case RENOGY_BIT_FLAGS:
			{	
				Serial.printf(" %04X\n", registerValue);
				Serial.printf("%42s", " ");
				for (int i = 15; i >= 0; i--) { 
					Serial.print(HEX_UPPER_CASE[i],false);
					if (i == 8) { 
						Serial.print(' ');
					}
				}
				Serial.printf("\n%42s", " ");
				for (int i = 15; i >= 0; i--) { 
					Serial.printf("%01d", (registerValue >> i) &0x01);
					if (i == 8) { Serial.print(' ',false);}
				}
				
				int registerBitFlagIndex = -1;
				int renogyBitFlagsSize = sizeof(renogyBitFlags) / sizeof(renogyBitFlags[0]);
				for (int i = 0; i < renogyBitFlagsSize; i++) {
					if (renogyBitFlags[i].registerAddress == registerAddress) {
						registerBitFlagIndex = i;
						break;
					}
				}

				if (registerBitFlagIndex < 0) { break; }
				do {
					Serial.printf("\n%42s", " ");
					int bit = renogyBitFlags[registerBitFlagIndex].bit;
					for (int i = 15; i > bit; i--) {logger.log('.',false); if (i == 8) { logger.log(' ',false); }}
					Serial.printf(((registerValue >> bit) &0x01) == 1 ? "1": "0");
					for (int i = bit; i > 0; i--) { logger.log('.',false); if (i == 8) { logger.log(' ',false); } }
					Serial.printf("  Bit %2d:", bit);
					Serial.printf(renogyBitFlags[registerBitFlagIndex].bitName,false);
					Serial.printf(((registerValue >> bit) &0x01) == 1 ? " TRUE": " FALSE",false);
					registerBitFlagIndex++;
				} while (registerBitFlagIndex < renogyBitFlagsSize && (renogyBitFlags[registerBitFlagIndex].registerAddress == registerAddress));
				break;
			}
	}
	Serial.println("");
	return (rr->bytesUsed / 2);
}