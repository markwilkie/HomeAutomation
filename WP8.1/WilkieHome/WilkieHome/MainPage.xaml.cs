using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Foundation;
using Windows.Foundation.Collections;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using Windows.UI.Xaml.Data;
using Windows.UI.Xaml.Input;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Navigation;
using Newtonsoft.Json;
using Windows.ApplicationModel.Background;
using NotificationsExtensions.TileContent;
using Windows.UI.Notifications;


// The Blank Page item template is documented at http://go.microsoft.com/fwlink/?LinkId=391641

namespace WilkieHome
{
    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class MainPage : Page
    {
        public MainPage()
        {
            this.InitializeComponent();

            this.NavigationCacheMode = NavigationCacheMode.Required;
        }

        /// <summary>
        /// Invoked when this page is about to be displayed in a Frame.
        /// </summary>
        /// <param name="e">Event data that describes how this page was reached.
        /// This parameter is typically used to configure the page.</param>
        protected override void OnNavigatedTo(NavigationEventArgs e)
        {
            this.RegisterBackgroundTask();

            GetSensorData();
        }

         public async void GetSensorData()
        {
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
                DeviceDataTextBox.Text = data.DeviceData1.ToString() + "°";
                DbDateTimeTextBox.Text = data.DbDateTime;

                //Update tile
                UpdateTile(data.DeviceData1.ToString(),data.DbDateTime);
            }
        }

         private static void UpdateTile(string temperature,string datetime)
         {
             // Create a notification for the Square150x150 tile using one of the available templates for the size.
             ITileSquare150x150Text01 square150x150Content = TileContentFactory.CreateTileSquare150x150Text01();
             square150x150Content.TextHeading.Text = temperature + "°";
             square150x150Content.TextBody1.Text = datetime;

             // Send the notification to the application? tile.
             TileUpdateManager.CreateTileUpdaterForApplication().Update(square150x150Content.CreateNotification());
         }

         private async void RegisterBackgroundTask()
         {
             var backgroundAccessStatus = await BackgroundExecutionManager.RequestAccessAsync();
             if (backgroundAccessStatus == BackgroundAccessStatus.AllowedMayUseActiveRealTimeConnectivity ||
                 backgroundAccessStatus == BackgroundAccessStatus.AllowedWithAlwaysOnRealTimeConnectivity)
             {
                 foreach (var task in BackgroundTaskRegistration.AllTasks)
                 {
                     if (task.Value.Name == "TemperatureBackgroundTask")
                     {
                         task.Value.Unregister(true);
                     }
                 }

                 BackgroundTaskBuilder taskBuilder = new BackgroundTaskBuilder();
                 taskBuilder.Name = "TemperatureBackgroundTask";
                 taskBuilder.TaskEntryPoint = "BackgroundTask.TemperatureBackgroundTask";
                 taskBuilder.SetTrigger(new TimeTrigger(15, false));
                 var registration = taskBuilder.Register();
             }
         }


    }

    class SensorData
    {
        public String DeviceName { get; set; }
        public Double DeviceData1 { get; set; }
        public Double DeviceData2 { get; set; }
        public String DbDateTime { get; set; }
    }
}
