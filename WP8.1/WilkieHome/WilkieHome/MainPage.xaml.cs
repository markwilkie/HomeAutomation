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

            /*
            client.Headers["Accept"] = "application/json";
            client.DownloadStringAsync(new Uri(uri));
            client.DownloadStringCompleted += (s1, e1) =>
            {
                var data = JsonConvert.DeserializeObject<SensorData>(e1.Result.ToString());
                DeviceDataTextBox.Text = data.DeviceData1.ToString();
                DbDateTimeTextBox.Text = data.DbDateTime;
            };
             * */
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
                DeviceDataTextBox.Text = data.DeviceData1.ToString();
                DbDateTimeTextBox.Text = data.DbDateTime;
            }
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
                 taskBuilder.TaskEntryPoint = "WilkieHome.TemperatureBackgroundTask";
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
