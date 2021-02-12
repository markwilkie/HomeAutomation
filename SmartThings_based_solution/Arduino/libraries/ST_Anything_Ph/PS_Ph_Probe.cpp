//******************************************************************************************
//  File: PS_pH_Probe.cpp
//  Authors: Mark Wilkie
//
//  Summary:  Uses AtlasScientific pH probe library
//
//  Change History:
//
//    Date        Who            What
//    ----        ---            ----
//    2021-01-27  Mark         Original Creation
//
//******************************************************************************************
#include "PS_Ph_Probe.h"

#include "Constants.h"
#include "Everything.h"
#include "ph_iso_grav.h"


namespace st
{
//private
	

//public
	//constructor - called in your sketch's global variable declaration section
	PS_Ph_Probe::PS_Ph_Probe(const __FlashStringHelper *name, unsigned int interval, int offset, int8_t pinA):
		PollingSensor(name, interval, offset),
		phProbe(pinA)
	{
		pinMode(pinA, INPUT);  //put pin in correct mode for pwm
	}
	
	//destructor
	PS_Ph_Probe::~PS_Ph_Probe()
	{
		
	}

	//SmartThings Shield data handler (receives configuration data from ST - polling interval, and adjusts on the fly)
	void PS_Ph_Probe::beSmart(const String &str)
	{
		String s = str.substring(str.indexOf(' ') + 1);

		if (s.toInt() != 0) {
			st::PollingSensor::setInterval(s.toInt() * 1000);
			if (st::PollingSensor::debug) {
				Serial.print(F("PS_Ph_Probe::beSmart set polling interval to "));
				Serial.println(s.toInt());
			}
		}
		else {
			if (st::PollingSensor::debug) 
			{
				Serial.print(F("PS_Ph_Probe::beSmart cannot convert "));
				Serial.print(s);
				Serial.println(F(" to an Integer."));
			}
		}
	}

	//initialization routine - get first set of readings and send to ST cloud
	void PS_Ph_Probe::init()
	{	
		phProbe.begin();		
		getData();
	}
	
	//function to get data from sensor and queue results for transfer to ST Cloud 
	void PS_Ph_Probe::getData()
	{
		dblpHValue = phProbe.read_ph();
        Everything::sendSmartString(getName() + " " + String(dblpHValue));
	}

	//Calibration
	void PS_Ph_Probe::calLow()
	{
		phProbe.cal_low();
	}
	void PS_Ph_Probe::calMid()
	{
		phProbe.cal_mid();
	}
	void PS_Ph_Probe::calHigh()
	{
		phProbe.cal_high();
	}
	void PS_Ph_Probe::calClear()
	{
		phProbe.cal_clear();
	}			
}