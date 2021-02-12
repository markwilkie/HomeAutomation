#include "PollingSensor.h"
#include <Wire.h>
#include "ph_iso_grav.h"


namespace st
{
	class PS_Ph_Probe : public PollingSensor
	{
		private:
			Gravity_pH_Isolated phProbe;  
			float dblpHValue;		//current pH value


		public:

			//constructor - called in your sketch's global variable declaration section
			PS_Ph_Probe(const __FlashStringHelper *name, unsigned int interval, int offset, int8_t pinA);

			//destructor
			virtual ~PS_Ph_Probe();

			//SmartThings Shield data handler (receives configuration data from ST - polling interval, and adjusts on the fly)
			virtual void beSmart(const String &str);

			//initialization routine
			virtual void init();

			//function to get data from sensor and queue results for transfer to ST Cloud
			virtual void getData();

			//calibration
			void calLow();
			void calMid();
			void calHigh();
			void calClear();

			//gets

			//sets

	};
}
