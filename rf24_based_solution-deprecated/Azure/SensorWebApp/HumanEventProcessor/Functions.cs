using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Data;
using System.Data.SqlClient;
using System.Configuration;
using Microsoft.Azure.WebJobs;
using Newtonsoft.Json.Linq;

namespace HumanEventProcessor
{
    public class Functions
    {
        // This function will get triggered/executed when a new message is written 
        // on an Azure Queue called queue.
        public static void ProcessQueueMessage([QueueTrigger("humanevents")] string message, TextWriter log)
        {
            Console.WriteLine("Raw JSON: " + message);

            //Parse and then call resolver
            JObject jsonObject;
            try
            {
                jsonObject = JObject.Parse(message);
            }
            catch
            {
                return;  //don't worry about garbage...
            }

            int unitNumJSON = (int)jsonObject["UnitNum"];
            string eventCodeTypeJSON = (string)jsonObject["EventCodeType"];
            string eventCodeJSON = (string)jsonObject["EventCode"];
            long deviceDate = (long)jsonObject["DeviceDate"];

            EventResolver eventResolver = new EventResolver(ConfigurationManager.ConnectionStrings["MyDBConnectionString"].ConnectionString);
            eventResolver.ResolveEventMsg(unitNumJSON, eventCodeTypeJSON, eventCodeJSON,deviceDate);
        }

        // Runs once every 10 minutes
        public static void TimerJob([TimerTrigger("00:10:00")] TimerInfo timer)
        {
            Console.WriteLine("Timer job fired!  Now resolving events.");

            //Init eventResolver 
            EventResolver eventResolver = new EventResolver(ConfigurationManager.ConnectionStrings["MyDBConnectionString"].ConnectionString);
            eventResolver.ResolveDbEvents();
        }
    }
}
