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
        public float VCC { get; set; }
        public float Temperature { get; set; }
        public float IntPinState { get; set; }
        public long DeviceDate { get; set; }
    }
}
