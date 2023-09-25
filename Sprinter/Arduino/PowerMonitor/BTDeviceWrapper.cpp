#include "BTDeviceWrapper.hpp"

//Sets name of device we want to connect to and its service/characteristic
void BTDeviceWrapper::initfoo(const char *_name,const char *_txSvcUUID,const char *_txChaUUID,const char *_rxSvcUUID,const char *_rxChaUUID)
{
	peripheryName=_name;
	txServiceUUID=_txSvcUUID;
	txCharacteristicUUID=_txChaUUID;
	rxServiceUUID=_rxSvcUUID;
	rxCharacteristicUUID=_rxChaUUID;	
}