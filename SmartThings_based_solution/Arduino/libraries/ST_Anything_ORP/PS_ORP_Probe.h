#include "PollingSensor.h"
#include <Wire.h>
#include <orp_iso_grav.h>


namespace st
{
	class PS_ORP_Probe : public PollingSensor
	{
		private:
			Gravity_ORP_Isolated orpProbe;
			float dblORPValue;		//current ORP value

		public:

			//constructor - called in your sketch's global variable declaration section
			PS_ORP_Probe(const __FlashStringHelper *name, unsigned int interval, int offset, int8_t pinA);

			//destructor
			virtual ~PS_ORP_Probe();

			//SmartThings Shield data handler (receives configuration data from ST - polling interval, and adjusts on the fly)
			virtual void beSmart(const String &str);

			//initialization routine
			virtual void init();

			//function to get data from sensor and queue results for transfer to ST Cloud
			virtual void getData();

			//Calibration
			void cal(float value);
        	void calClear();

			//gets

			//sets

	};
}
