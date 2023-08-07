//put your variable definitions here for the data you want to send
//THIS MUST BE EXACTLY THE SAME ON THE OTHER ARDUINO
//
// (easy transfer library)

struct SERIAL_PAYLOAD_STRUCTURE
{
  float stateOfCharge;
  float stateOfWater;
  float volts;
  float chargeAh;
  float drawAh;
  float batteryHoursRem;
  float waterDaysRem;
};