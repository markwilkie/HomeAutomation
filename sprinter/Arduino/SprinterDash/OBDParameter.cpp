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
 */
OBDParameter:: OBDParameter (char _name[STR_LNGTH],
							   char _units[STR_LNGTH],
							   OBD_PID _pid,
							   OBD_PID_SIZE _size,
							   bool _signed,
							   OBD_MODE_REQ _mode,
							   float _slope,
							   float _offset,
							   cAcquireCAN *_portNum,
                               bool _extended)
{
	UINT8 strSize,i;

	//set simple members, name, units etc

	//copy strings
	for (i=0; i < STR_LNGTH; i++)
	{
		name[i]  = _name[i];
		units[i] = _units[i];
	}

	//set parameter ID
	pid = _pid;

	//set OBD data size
	size = _size;

	//set OBD mode
	dataMode = _mode;

	//set default slope and offset
	m =  _slope;
	b =  _offset;

	//assign scheduler (set port number)
	portNum = _portNum;

	//set periodic rate at which we will make OBD requests
	//setting this QUERY_MSG means that only one message will be transmitted per scheduler "tick". I.E. this would be the same rate
	//that the timer interrupt for the acquisition scheduler runs at
	TXFrame.rate = QUERY_MSG;

	/*
	 * This is where the raw CAN packets to be passed into the lower level scheduler for transmission and reception.
	 */
	
	//
	//********** TX REQUEST FRAME ****************
	//where OBD PID response is CANID: BYTE_0, BYTE_1, BYTE_2, BYTE_3, BYTE_4, BYTE_5, BYTE_6, BYTE_7 
	//
	//							    ----------upper payload-------  	   -----------------lower payload----------------
	//
	//							   | add bytes (2) | mode | PID  |  value[0] (0x55 = NA) |  value[1]  |   value[2]  |  value[3]  |   NA  |
	//
	//
	//NOTE: for transmisison requests now we are going to use the main broadcast message to speak to all ECU's 0x7DF
	//
	TXFrame.ID   	 = _extended ? 0x18DB33F1 : 0x7DF;
    TXFrame.U.b[0]   = 2;
    TXFrame.U.b[1]   = (UINT8)dataMode;
	TXFrame.U.b[2]   = (UINT8)pid;
	TXFrame.U.b[3]   = 0x55;                            
	TXFrame.U.P.upperPayload = 0x55555555;

	//add message to acquisition list in associated acquire class.
	portNum->addMessage(&TXFrame, TRANSMIT);
		
	//
	//********** RX RECEIVE FRAME ****************
	//
	//where OBD PID response is CANID: BYTE_0, BYTE_1, BYTE_2, BYTE_3, BYTE_4, BYTE_5, BYTE_6, BYTE_7 
	//
	//							    ----------upper payload-------  	   -----------------lower payload----------------
	//
	//							   | add bytes | mode & 0x40 (ack) | PID |  value[0] |  value[1]  |   value[2]  |  value[3]  |   NA  |
	//
	//NOTE: for now we are going to assume the main ECU response 0x7E8
	//
	RXFrame.ID   	 = _extended ? 0x18DAF10E : 0x7E8;  
    RXFrame.U.b[0]   = (UINT8)size;
    RXFrame.U.b[1]   = (UINT8)dataMode;
	RXFrame.U.b[2]   = (UINT8)pid;
	RXFrame.U.b[3]   = 0x00;

	//(we have not received anything, so really the payload irrelevant)
	RXFrame.U.P.upperPayload = 0x00000000;

	//add message to acquisition list for periodic reception in the acquire class
	portNum->addMessage(&RXFrame, RECEIVE);


	//add message to static list of all OBDMessages created
	OBDList[listIdx] = this;
	listIdx = listIdx < MAX_NUM_PIDS ? listIdx +1 : MAX_NUM_PIDS;
}