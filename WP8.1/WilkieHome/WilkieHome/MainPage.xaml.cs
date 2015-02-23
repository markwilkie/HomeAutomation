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
using Windows.Storage;


// The Blank Page item template is documented at http://go.microsoft.com/fwlink/?LinkId=391641

namespace WilkieHome
{

    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class MainPage : Page
    {
        static string taskName = "BackgroundTask";
        static string taskNameSpace = "BackgroundTask";

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
            //Grab application data
            GetAppData();

            //Register background task
            this.RegisterBackgroundTask();

            //Get sensor data to seed values
            GetSensorData();
        }

        private void Refresh_Button_Click(object sender, RoutedEventArgs e)
        {
            GetSensorData();
        }

        private void GetAppData()
        {
            Windows.Storage.ApplicationDataContainer localSettings = Windows.Storage.ApplicationData.Current.LocalSettings;
            if (localSettings.Values.ContainsKey("BGMinutes"))
            {
                BGMinutes.Text=(string)localSettings.Values["BGMinutes"];
            }
            if (localSettings.Values.ContainsKey("ChargeHours"))
            {
                ChargeHours.Text = (string)localSettings.Values["ChargeHours"];
            }
            if (localSettings.Values.ContainsKey("CheckHourStart"))
            {
                CheckHourStart.Text = (string)localSettings.Values["CheckHourStart"];
            }
            if (localSettings.Values.ContainsKey("CheckHourEnd"))
            {
                CheckHourEnd.Text = (string)localSettings.Values["CheckHourEnd"];
            }
        }

        private void Save_Settings_Button_Click(object sender, RoutedEventArgs e)
        {
            //Set settings
            Windows.Storage.ApplicationDataContainer localSettings = Windows.Storage.ApplicationData.Current.LocalSettings;
            localSettings.Values["BGMinutes"] = BGMinutes.Text;
            localSettings.Values["ChargeHours"] = ChargeHours.Text;
            localSettings.Values["CheckHourStart"] = CheckHourStart.Text;
            localSettings.Values["CheckHourEnd"] = CheckHourEnd.Text;

            //Re-register b/g tasks
            this.RegisterBackgroundTask();
        }

         private async void GetSensorData()
         {
             string uri = "http://sensors.cloudapp.net/Sensor/CurrentTemperature/" + DeviceNumberTextBox.Text;
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
             uint minuteIncrements=15;  //default

            //Grab increments from isolated storage
            var localSettings = ApplicationData.Current.LocalSettings;
            if (localSettings.Values.ContainsKey("MinuteIncrements"))
                minuteIncrements = Convert.ToUInt16(localSettings.Values["MinuteIncrements"]);

             //Setup background task
             var backgroundAccessStatus = await BackgroundExecutionManager.RequestAccessAsync();
             if (backgroundAccessStatus == BackgroundAccessStatus.AllowedMayUseActiveRealTimeConnectivity ||
                 backgroundAccessStatus == BackgroundAccessStatus.AllowedWithAlwaysOnRealTimeConnectivity)
             {
                 foreach (var task in BackgroundTaskRegistration.AllTasks)
                 {
                     if (task.Value.Name == taskName)
                     {
                         task.Value.Unregister(true);
                     }
                 }

                 BackgroundTaskBuilder taskBuilder = new BackgroundTaskBuilder();
                 taskBuilder.Name = taskName;
                 taskBuilder.TaskEntryPoint = taskName+"."+taskNameSpace;
                 taskBuilder.SetTrigger(new TimeTrigger(minuteIncrements, false));
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
