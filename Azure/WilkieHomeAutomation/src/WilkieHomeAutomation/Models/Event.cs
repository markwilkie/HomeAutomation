using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace WilkieHomeAutomation.Models
{
    public class Event
    {
        //Class vars
        Dictionary<string, string> eventTypeDict;
        Dictionary<string, string> eventCodeDict;

        //Constructor
        public Event()
        {
            //Load the dictionaries
            eventTypeDict = new Dictionary<string, string>();
            eventTypeDict.Add("M", "Motion Detector");
            eventTypeDict.Add("O", "Door");

            eventCodeDict = new Dictionary<string, string>();
            eventCodeDict.Add("O", "Opened");
            eventCodeDict.Add("C", "Closed");
            eventCodeDict.Add("D", "Detected");
        }

        //Property members
        public int ID { get; set; }
        public int UnitNum { get; set; }
        public string EventCodeType { get; set; }
        public string EventCode { get; set; }
        public long DeviceDate { get; set; }


        //Non mapped fields for easier use
        public string EventTypeDesc { get { return eventTypeDict[EventCodeType]; } }
        public string EventDesc { get { return eventCodeDict[EventCode]; } }
    }
}