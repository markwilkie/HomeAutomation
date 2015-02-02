using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.WindowsAzure;
using Microsoft.WindowsAzure.Diagnostics;
using Microsoft.WindowsAzure.ServiceRuntime;
using Microsoft.WindowsAzure.Storage;
//using Twilio;
using System.Data.SqlClient;
using System.Data;
using System.Text;
using Microsoft.Owin.Hosting;


namespace WorkerRole1
{
    public class WorkerRole : RoleEntryPoint
    {
        private readonly CancellationTokenSource cancellationTokenSource = new CancellationTokenSource();
        private readonly ManualResetEvent runCompleteEvent = new ManualResetEvent(false);
        private IDisposable sensorEndPoint = null;

        public override void Run()
        {
            try
            {
                Trace.TraceInformation("WorkerRole1 is running");

                /*
                // Find your Account Sid and Auth Token at twilio.com/user/account 
                string AccountSid = "AC86c0b365a20c44d5871a8f3b52706377";
                string AuthToken = "0329c66950f39a3707fb6112398601eb";
                TwilioRestClient twilioClient = new TwilioRestClient(AccountSid, AuthToken);
                 * */

                while (true)
                {
                    DateTime dateNow = DateTime.Now;
                    DateTime date = new DateTime(dateNow.Year, dateNow.Month, dateNow.Day,21,30,0);
                    TimeSpan ts;
                    if(date>dateNow)
                        ts = date - dateNow;
                    else
                        ts = date.AddDays(1) - dateNow;
 
                    //waits certan time and run the code
                    Trace.TraceInformation("Waiting total hours: "+ts.TotalHours.ToString());
                    Task.Delay(ts).Wait();

                    Thread.Sleep(10000);
                    Trace.WriteLine("Working", "Information");
                }
            }
            catch (Exception e)
            {
                Trace.WriteLine("Exception during Run: " + e.ToString());
                // Take other action as needed.
            }
        }

        public override bool OnStart()
        {
            // Set the maximum number of concurrent connections
            ServicePointManager.DefaultConnectionLimit = 12;

            // For information on handling configuration changes
            // see the MSDN topic at http://go.microsoft.com/fwlink/?LinkId=166357.

            // Startup temperature endpoint
            var endpoint = RoleEnvironment.CurrentRoleInstance.InstanceEndpoints["SensorEndPoint"];
            string baseUri = String.Format("{0}://{1}", endpoint.Protocol, endpoint.IPEndpoint);
            Trace.TraceInformation(String.Format("Starting temperature at {0}", baseUri),"Information");
            sensorEndPoint = WebApp.Start<Startup>(new StartOptions(url: baseUri));

            return base.OnStart(); ;
        }

        public override void OnStop()
        {
            Trace.TraceInformation("WorkerRole1 is stopping");

            this.cancellationTokenSource.Cancel();
            this.runCompleteEvent.WaitOne();

            if (sensorEndPoint != null)
            {
                sensorEndPoint.Dispose();
            }

            base.OnStop();

            Trace.TraceInformation("WorkerRole1 has stopped");
        }

        private async Task RunAsync(CancellationToken cancellationToken)
        {
            // TODO: Replace the following with your own logic.
            while (!cancellationToken.IsCancellationRequested)
            {
                Trace.TraceInformation("Working");
                await Task.Delay(1000);
            }
        }
    }
}

