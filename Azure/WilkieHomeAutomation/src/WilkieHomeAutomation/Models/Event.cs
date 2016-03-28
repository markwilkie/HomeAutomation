using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace WilkieHomeAutomation.Models
{
    public class Event
    {
        public int ID { get; set; }
        public int UnitNum { get; set; }
        public string EventCodeType { get; set; }
        public string EventCode { get; set; }
        public long DeviceDate { get; set; }
    }
}