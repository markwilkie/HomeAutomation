using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
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
using Windows.ApplicationModel.Background;
using NotificationsExtensions.TileContent;
using Windows.UI.Notifications;
using Windows.Storage;

using WilkieHome.Model;
using WilkieHome.VM;


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
        private ViewModel vm;

        public MainPage()
        {
            this.InitializeComponent();
            vm = new ViewModel();

            this.NavigationCacheMode = NavigationCacheMode.Required;
        }

        /// <summary>
        /// Invoked when this page is about to be displayed in a Frame.
        /// </summary>
        /// <param name="e">Event data that describes how this page was reached.
        /// This parameter is typically used to configure the page.</param>
        protected override async void OnNavigatedTo(NavigationEventArgs e)
        {
            //Get sensor data to seed values
            await vm.GetSensorData();
            await vm.GetEventData();

            //Set data context
            //this.DataContext = from SensorData in vm.sensorDataList where SensorData.UnitNum >= 0 select SensorData;
            SensorList.DataContext = vm.sensorDataList;
            EventList.DataContext = vm.eventDataList;
        }

        private void SensorList_ItemClick(object sender, ItemClickEventArgs e)
        {
            SensorData sensorData = e.ClickedItem as SensorData;
            Frame.Navigate(typeof(DetailPage),sensorData);
        }

        private void EventList_ItemClick(object sender, ItemClickEventArgs e)
        {
            EventData eventData = e.ClickedItem as EventData;
            Frame.Navigate(typeof(DetailPage), eventData);
        }
    }
}
