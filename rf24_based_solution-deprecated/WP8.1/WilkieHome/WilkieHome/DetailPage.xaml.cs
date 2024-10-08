﻿using System;
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
    public sealed partial class DetailPage : Page
    {
        private ViewModel vm;

        public DetailPage()
        {
            this.InitializeComponent();
            vm = new ViewModel();

            this.NavigationCacheMode = NavigationCacheMode.Required;
            Windows.Phone.UI.Input.HardwareButtons.BackPressed += OnBackPressed;
        }
        private void OnBackPressed(object sender, Windows.Phone.UI.Input.BackPressedEventArgs e)
        {
            if (Frame.CanGoBack)
            {
                e.Handled = true;
                Frame.GoBack();
            }
        }

        /// <summary>
        /// Invoked when this page is about to be displayed in a Frame.
        /// </summary>
        /// <param name="e">Event data that describes how this page was reached.
        /// This parameter is typically used to configure the page.</param>
        protected override async void OnNavigatedTo(NavigationEventArgs e)
        {
            if(e.Parameter is SensorData)
            {
                SensorData sensorData = e.Parameter as SensorData;
                DeviceNumberTextBox.Text = sensorData.UnitNum.ToString();
            }
            if (e.Parameter is EventData)
            {
                EventData eventData = e.Parameter as EventData;
                EventDeviceNumberTextBox.Text = eventData.UnitNum.ToString();
                EventCodeType.Text = eventData.EventCodeType.ToString();
            }

            //Grab application data
            GetAppData();

            //Get sensor data to seed values
            await vm.GetSensorData(DeviceNumberTextBox.Text);
            await vm.GetEventData(EventDeviceNumberTextBox.Text, EventCodeType.Text);

            //Set data context
            //this.DataContext = from SensorData in vm.sensorDataList where SensorData.UnitNum >= 0 select SensorData;
            DevicePanel.DataContext = vm.sensorData;
            EventPanel.DataContext = vm.eventData;
        }

        private async void Refresh_Button_Click(object sender, RoutedEventArgs e)
        {
            await vm.GetSensorData(DeviceNumberTextBox.Text);
            await vm.GetEventData(EventDeviceNumberTextBox.Text, EventCodeType.Text);

            //Reset data context
            DevicePanel.DataContext = vm.sensorData;
            EventPanel.DataContext = vm.eventData;
        }

        private void GetAppData()
        {
            Windows.Storage.ApplicationDataContainer localSettings = Windows.Storage.ApplicationData.Current.LocalSettings;
            if (localSettings.Values.ContainsKey("MinuteIncrements"))
            {
                BGMinutes.Text = (string)localSettings.Values["MinuteIncrements"];
            }
            /*
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
             * */
        }

        private void Save_Settings_Button_Click(object sender, RoutedEventArgs e)
        {
            //Set settings
            Windows.Storage.ApplicationDataContainer localSettings = Windows.Storage.ApplicationData.Current.LocalSettings;
            localSettings.Values["MinuteIncrements"] = BGMinutes.Text;
            //localSettings.Values["CheckHourStart"] = CheckHourStart.Text;
            //localSettings.Values["CheckHourEnd"] = CheckHourEnd.Text;
        }
    }
}
