using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.ComponentModel;

namespace WilkieHome.Model
{
    public class SensorData
    {
        public int UnitNum { get; set; }
        public double? VCC { get; set; }
        public double? Temperature { get; set; }
        public string FormattedTemperature { get { return GetFormattedTemperature(); } }
        public int? IntPinState { get; set; }
        public DateTime DeviceDateTime { get; set; }

        private string GetFormattedTemperature()
        {
            return Math.Round((double)Temperature, 1).ToString() + "°";
        }
    }

    public class EventData
    {
        public int UnitNum { get; set; }
        public char EventCodeType { get; set; }
        public string FormattedEventCodeType { get { return GetFormattedEventCodeType(); } }
        public char EventCode { get; set; }
        public string FormattedEventCode { get { return GetFormattedEventCode(); } }
        public DateTime DeviceDateTime { get; set; }

        private string GetFormattedEventCodeType()
        {
            string typeText = EventCodeType.ToString();
            if (EventCodeType == 'O') typeText = "Door";
            if (EventCodeType == 'M') typeText = "Motion";

            return typeText;

        }

        private string GetFormattedEventCode()
        {
            string statusText = EventCode.ToString();
            if (EventCode == 'C') statusText = "Closed";
            if (EventCode == '-') statusText = "No Events";
            if (EventCode == 'D') statusText = "Detected";

            return statusText;
        }
    }
}
