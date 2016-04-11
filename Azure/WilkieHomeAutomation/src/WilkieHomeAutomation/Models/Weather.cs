using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace WilkieHomeAutomation.Models
{
    public class Weather
    {
        public int ID { get; set; }
        public int UnitNum { get; set; }
        public decimal Temperature { get; set; }
        public decimal Humidity { get; set; }
        public decimal WindSpeed { get; set; }
        public decimal WindDirection { get; set; }
        public long ReadingDate { get; set; }
    }
}
