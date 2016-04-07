using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using System.ComponentModel.DataAnnotations;

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
        //public DateTime DeviceDateTime { get; set; }
        //public DateTime LastStateDeviceDT { get; set; }
        //public DateTime LastEventDeviceDT { get; set; }


        //Non mapped fields for easier use
        public string DeviceDateTimeStr { get { return GetFormattedDate(DeviceDate); } }
        public string LastState {  get { return GetAge(LastStateDT); } }
        public string LastEvent { get { return GetAge(LastEventDT); } }

        private string GetFormattedDate(long epoch)
        {
            //Convert epoch to DT
            DateTime epochDT = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc);
            DateTime deviceDT = epochDT.AddSeconds(epoch);
            return deviceDT.ToString(@"MM-dd HH:mm tt");
        }

        private string GetAge(long epoch)
        {
            if (epoch == 0)
                return "n/a";

            //Convert epoch to DT
            DateTime epochDT = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc);
            DateTime deviceDT = epochDT.AddSeconds(epoch);

            //Adjust for TZ
            TimeZoneInfo zone = TimeZoneInfo.FindSystemTimeZoneById("Pacific Standard Time");
            DateTime now = TimeZoneInfo.ConvertTime(DateTime.Now, TimeZoneInfo.Local, zone);
            TimeSpan timeSpan = now - deviceDT;

            //Create return string based on span
            if (timeSpan.TotalDays > 1)
                return (int)timeSpan.TotalDays + "D";
            if (timeSpan.TotalHours > 1)
                return (int)timeSpan.TotalHours + "H";

            return (int)timeSpan.TotalMinutes + "M";
        }
    }
}
