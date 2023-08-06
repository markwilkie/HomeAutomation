//put your variable definitions here for the data you want to send
//THIS MUST BE EXACTLY THE SAME ON THE OTHER ARDUINO
//
// (easy transfer library)

struct SERIAL_PAYLOAD_STRUCTURE
{
  float_t stateOfCharge;
  float_t volts;
  float_t solarAh;
  float_t drawAh;
  float_t hoursRem;
};