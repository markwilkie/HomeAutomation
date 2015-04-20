using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Common
{
    public class SensorData
    {
        public int UnitNum { get; set; }
        public double? VCC { get; set; }
        public double? Temperature { get; set; }
        public int? IntPinState { get; set; }
        public DateTime DeviceDateTime { get; set; }

        public string GetFormattedTemperature()
        {
            return Math.Round((double)Temperature, 1).ToString() + "°";
        }
    }

    public class EventData
    {
        public int UnitNum { get; set; }
        public char EventCodeType { get; set; }
        public char EventCode { get; set; }
        public DateTime DeviceDateTime { get; set; }
    }
}
