using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace WilkieHomeAutomation.Models
{
    public class State
    {
        public int ID { get; set; }
        public int UnitNum { get; set; }
        public decimal VCC { get; set; }
        public decimal Temperature { get; set; }
        public int IntPinState { get; set; }
        public long DeviceDate { get; set; }
    }
}
