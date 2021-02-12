using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace WilkieHomeAutomation.Models
{
    public class ResolvedEvent
    {
        //Property members
        public int ID { get; set; }
        public string ResolutionName { get; set; }
        public string Resolution { get; set; }
        public string Confidence { get; set; }
        public string JSONPath { get; set; }
        public long DeviceEpoch { get; set; }

        //Non mapped fields for easier use
        public string DeviceDateTimeStr { get { return GetFormattedDate(DeviceEpoch); } }

        private string GetFormattedDate(long epoch)
        {
            //Convert epoch to DT
            DateTime epochDT = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc);
            DateTime deviceDT = epochDT.AddSeconds(epoch);
            return deviceDT.ToString(@"MM-dd HH:mm tt");
        }
    }
}
