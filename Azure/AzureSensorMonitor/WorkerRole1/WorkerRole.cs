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
using Twilio;
using System.Data.SqlClient;
using System.Data;

namespace WorkerRole1
{
    public class WorkerRole : RoleEntryPoint
    {
        private readonly CancellationTokenSource cancellationTokenSource = new CancellationTokenSource();
        private readonly ManualResetEvent runCompleteEvent = new ManualResetEvent(false);

        SqlConnection sqlConnection;

        public override void Run()
        {
            try
            {
                Trace.TraceInformation("WorkerRole1 is running");

                // Find your Account Sid and Auth Token at twilio.com/user/account 
                string AccountSid = "AC86c0b365a20c44d5871a8f3b52706377";
                string AuthToken = "0329c66950f39a3707fb6112398601eb";
                TwilioRestClient twilioClient = new TwilioRestClient(AccountSid, AuthToken);

                while (true)
                {
                    //Read database
                    String sqlCommmand = "select * from SensorData";
                    SqlCommand command = new SqlCommand(sqlCommmand);
                    SqlDataReader reader = command.ExecuteReader();
                    while (reader.Read())  //loop through rows
                    {
                        IDataRecord record = (IDataRecord)reader;
                        Console.WriteLine(String.Format("{0}, {1}", record[0], record[1]));
                    }
                    reader.Close();

                    // Send an SMS message.
                    Message result = twilioClient.SendMessage("+16084339205", "+12063311936", "This is my SMS message.");
                    if (result.RestException != null)
                    {
                        //an exception occurred making the REST call
                        Trace.WriteLine("Problem sending: " + result.RestException.Message);
                    }

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

            try
            {
                //SQL
                string sqlConnectionString = "Data Source=vulxjerrb6.database.windows.net;Initial Catalog=PowerPal;User ID=dba;pwd=1234Data!";
                sqlConnection = new SqlConnection(sqlConnectionString);
                //
                // Open the SqlConnection.
                //
                sqlConnection.Open();
            }
            catch (Exception e) { }

            bool result = base.OnStart();

            Trace.TraceInformation("WorkerRole1 has been started");

            return result;
        }

        public override void OnStop()
        {
            Trace.TraceInformation("WorkerRole1 is stopping");

            this.cancellationTokenSource.Cancel();
            this.runCompleteEvent.WaitOne();

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

