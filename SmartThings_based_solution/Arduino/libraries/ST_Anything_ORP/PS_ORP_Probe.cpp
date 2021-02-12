//******************************************************************************************
//  File: PS_ORP_Probe.cpp
//  Authors: Mark Wilkie
//
//  Summary:  Uses AtlasScientific ORP probe library
//
//  Change History:
//
//    Date        Who            What
//    ----        ---            ----
//    2021-01-27  Mark         Original Creation
//
//******************************************************************************************

#include "PS_ORP_Probe.h"

#include "Constants.h"
#include "Everything.h"
#include "orp_iso_grav.h"


namespace st
{
//private
	

//public
	//constructor - called in your sketch's global variable declaration section
	PS_ORP_Probe::PS_ORP_Probe(const __FlashStringHelper *name, unsigned int interval, int offset, int8_t pinA):
		PollingSensor(name, interval, offset),
		orpProbe(pinA)
	{
		pinMode(pinA, INPUT);  //put pin in correct mode for pwm
	}
	
	//destructor
	PS_ORP_Probe::~PS_ORP_Probe()
	{
		
	}

	//SmartThings Shield data handler (receives configuration data from ST - polling interval, and adjusts on the fly)
	void PS_ORP_Probe::beSmart(const String &str)
	{
		String s = str.substring(str.indexOf(' ') + 1);

		if (s.toInt() != 0) {
			st::PollingSensor::setInterval(s.toInt() * 1000);
			if (st::PollingSensor::debug) {
				Serial.print(F("PS_ORP_Probe::beSmart set polling interval to "));
				Serial.println(s.toInt());
			}
		}
		else {
			if (st::PollingSensor::debug) 
			{
				Serial.print(F("PS_ORP_Probe::beSmart cannot convert "));
				Serial.print(s);
				Serial.println(F(" to an Integer."));
			}
		}
	}

	//initialization routine - get first set of readings and send to ST cloud
	void PS_ORP_Probe::init()
	{
		orpProbe.begin();		
		getData();
	}
	
	//function to get data from sensor and queue results for transfer to ST Cloud 
	void PS_ORP_Probe::getData()
	{
		dblORPValue = orpProbe.read_orp();
        Everything::sendSmartString(getName() + " " + String(dblORPValue));
	}

	//Calibration
	void PS_ORP_Probe::cal(float value)
	{
		orpProbe.cal(value);
	}

    void PS_ORP_Probe::calClear()
	{
		orpProbe.cal_clear();
	}
	
}