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
using Windows.UI.Notifications;
using Windows.Data.Xml.Dom;

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

        private void UpdateTile()
        {
            XmlDocument xml = TileUpdateManager.GetTemplateContent(TileTemplateType.TileWide310x150ImageAndText01);

            //XmlDocument xml = TileUpdateManager.GetTemplateContent(TileTemplateType.TileWideText01);
            XmlNodeList nodes = xml.GetElementsByTagName("text");
            nodes[0].InnerText = "This is the text";

            TileNotification notification = new TileNotification(xml);
            TileUpdateManager.CreateTileUpdaterForApplication().Update(notification);
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
