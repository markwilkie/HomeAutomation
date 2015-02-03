using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

// Added during quickstart
using Windows.ApplicationModel.Background;
using Windows.Data.Xml.Dom;
using Windows.UI.Notifications;

using System.Net.Http;
using System.Net.Http.Headers;
using Newtonsoft.Json;
using NotificationsExtensions.TileContent;
using Windows.UI.Notifications;

namespace WilkieHome
{
    public sealed class TemperatureBackgroundTask : IBackgroundTask
    {
        public async void Run(IBackgroundTaskInstance taskInstance)
        {
            // Get a deferral, to prevent the task from closing prematurely 
            // while asynchronous code is still running.
            BackgroundTaskDeferral deferral = taskInstance.GetDeferral();

            // get temp
            string uri = "http://sensors.cloudapp.net/Sensor/CurrentTemperature";
            HttpClient client = new HttpClient();

            client.BaseAddress = new Uri(uri);
            client.DefaultRequestHeaders.Accept.Clear();
            client.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
            var response = await client.GetAsync(uri);
            if (response.IsSuccessStatusCode)
            {
                var responseText = await response.Content.ReadAsStringAsync();
                var data = JsonConvert.DeserializeObject<SensorData>(responseText);

                //Update tile
                UpdateTile(data.DeviceData1.ToString() + "\n" + data.DbDateTime);
            }

            // Inform the system that the task is finished.
            deferral.Complete();
        }

        private static void UpdateTile(string temperature)
        {
            // Create a notification for the Square150x150 tile using one of the available templates for the size.
            ITileSquare150x150Text04 square150x150Content = TileContentFactory.CreateTileSquare150x150Text04();
            square150x150Content.TextBodyWrap.Text = temperature;

            // Send the notification to the application? tile.
            TileUpdateManager.CreateTileUpdaterForApplication().Update(square150x150Content.CreateNotification());
        }
    }
}