using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace WilkieHomeAutomation.Models
{
    public class Device
    {
        public int ID { get; set; }
        public int UnitNum { get; set; }
        public string Description { get; set; }
        public long LastStateDT { get; set; }
        public long LastEventDT { get; set; }
        public long DeviceDate { get; set; }
    }
}
