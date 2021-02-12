using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using System.Configuration;
using Microsoft.Azure.WebJobs;
using Microsoft.WindowsAzure.Storage;
using Microsoft.WindowsAzure.Storage.Queue;
using Microsoft.Azure.WebJobs.Extensions.WebHooks;

namespace HumanEventProcessor
{
    // To learn more about Microsoft Azure WebJobs SDK, please see http://go.microsoft.com/fwlink/?LinkID=320976
    class Program
    {
        // Please set the following connection strings in app.config for this WebJob to run:
        // AzureWebJobsDashboard and AzureWebJobsStorage
        static void Main()
        {
            var config = new JobHostConfiguration();
            config.Tracing.ConsoleLevel = TraceLevel.Verbose;
            //config.UseDevelopmentSettings();

            config.UseTimers();

            var host = new JobHost(config);

            //Init storage
            InitializeStorage();

            // The following code ensures that the WebJob will be running continuously
            host.RunAndBlock();
        }

        static private void InitializeStorage()
        {
            // Open storage account 
            var storageAccount = CloudStorageAccount.Parse(ConfigurationManager.ConnectionStrings["AzureWebJobsStorage"].ConnectionString);
            
            Trace.TraceInformation("Creating queues");
            CloudQueueClient queueClient = storageAccount.CreateCloudQueueClient();
            var reviewEventsQueue = queueClient.GetQueueReference("humanevents");
            reviewEventsQueue.CreateIfNotExists();
            Trace.TraceInformation("Storage initialized");
        }
    }
}
