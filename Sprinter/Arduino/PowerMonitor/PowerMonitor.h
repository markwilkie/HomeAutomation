#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

class BTDevice;

struct BLE_SEMAPHORE
{
	BTDevice *btDevice;   			//Pointer to our device context ball
	uint32_t startTime;				//When we sent the request
	uint16_t expectedBytes;			//The two bytes in response we're waiting for
    boolean waitingForConnection;	//True when blocking for a connection callback
	boolean waitingForResponse;		//True when blocking for a response callback to a command
};

#endif