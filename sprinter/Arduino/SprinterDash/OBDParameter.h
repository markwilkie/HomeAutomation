#include <Arduino.h>

#ifndef OBDParameter_H
#define OBDParameter_H

/**
 * OBD class that is used to create a "new" PID parameter. All attributes of the PID message are defined here.
 * Name, slope, offset, parameter number etc. The intention is for the user to define these
 * in the sketch passing a reference to the appropriate acquisition scheduler (physical can port) for periodic RX/TX of mesages.
 * 
 * @author D.Kasamis - dan@togglebit.net
 * @version 1.0
 */
class OBDParameter 
{
public:
	/**
	* This is the constructor for an OBD parameter. One class should be created and initialized for each parameter ID.
	* If you intend to add parameter ID's that are not in this class already, you'll need to simply add it to the ENUM list "OBD_PID"
	* All other parameters can be dervied from the general OBD2 specficaitons (http://en.wikipedia.org/wiki/OBD-II_PIDs).
	* 
	* 
	* @param _name    -  string describing channel name
	* @param _units   -  string describing engineering units
	* @param _pid     -  parameter ID number
	* @param _size    -  size of the mesage (8, 16 bits etc)
	* @param _signed  -  flag indicating if it is a signed number (sint, uint, etc)
	* @param _mode    -  this is the OBD mode of data we have requested (as provided by the standard) current data, freeze, etc,
	* @param _slope   -  this OBD class assumes a linear relationship between counts and units. This is the slope.
	* @param _offset  -  this sensor class assumes a linear relationship between counts and units. This is the offset.
	* @param _portNum -  physical CAN port to be used for this OBD parameter
	* @param _extended-  indicate we are using OBD2 extended ID's
	*/
	OBDParameter (char _name[STR_LNGTH],
				   char _units[STR_LNGTH],
				   OBD_PID _pid,
				   OBD_PID_SIZE _size,
				   bool _signed,
				   OBD_MODE_REQ _mode,
				   float _slope,
				   float _offset,
				   cAcquireCAN *_portNum,
                   bool _extended);
	/**
	 * Retreive OBD2 signal data in floating point engineering units. Note the floating point/EU conversion only occurs
	 * when this method is called
	 * 
	 * @return floating point engineering unit representation of OBD2 signal
	 */
	float getData();
	/**
	 * This method is responsible for extracting the data portion of a received CAN frame (OBD message) based upon 8,16,32bit message sizes.
	 * This works in UINT32 data type (calling funciton needs to cast this for sign support).
	 * 
	 * @return UINT32 - integer that represents OBD message data (no scaling or sign applied)
	 */
	UINT32 getIntData();
	/**
	 * this handler is called when a CAN frame ID matching a OBD2 response message is received
	 * 
	 * @param I - pointer to the received CAN frame
	 * @return -  bool this is the proper response for this PID
	 */
	bool receiveFrame(RX_CAN_FRAME *I);
	/**
	 * Retreive the string representing the OBD2 signal name
	 * 
	 * @return - pointer to null-terminated ASCII char string for signal name
	 */
	char* getName();
	/**
	 * Retreive the string representing the OBD2 signal's engineering units
	 * 
	 * @return - pointer to null-terminated ASCII char string for signal units
	 */
	char* getUnits();

protected:
	private:
	/**
	 * string representing parameter name
	 */
	char name[STR_LNGTH];

	/**
	 * string defining units
	 */
	char units[STR_LNGTH];    

	/**
	 * ID number
	 */
	OBD_PID pid;

	/**
	 * size of the mesage (8, 16 bits etc)
	 */
	OBD_PID_SIZE size;

	/**
	 * this is the OBD mode of data we have requested (as provided by the standard) current data, freeze, etc, 
	 */
	OBD_MODE_REQ dataMode;

	/**
	 * The OBD class assumes a linear relationship between digital representation and engineering units. This is the slope and offsett (Y = mX+b)
	 */
	float m; 
	float b;

	/**
	 * flag indicating if it is a signed number (sint, uint, etc)
	 */
	bool sign; 

	/**
	 * periodic scheduler that is to be used aka - physical port to be used
	 */
	cAcquireCAN *portNum;

	/**
	 * this is the transmitit frame that is used to make the OBD data request CAN receiver
	 */
	cOBDTXFrame TXFrame;

	/**
	 * this is the receive frame that is used to make the OBD data request to the CAN receiver
	 */
	cOBDRXFrame RXFrame;

	/**
	 * this is a list that keeps track of all of the OBD2 PID created
	 */
	static OBDParameter *OBDList[MAX_NUM_PIDS];
	static UINT8  listIdx;
};

#endif
