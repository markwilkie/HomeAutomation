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
using Windows.Storage;

using WilkieHome.Model;
using WilkieHome.VM;

namespace BackgroundTask
{
    public sealed class BackgroundTask : IBackgroundTask
    {
        private ViewModel vm;

        public BackgroundTask()
        {
            vm = new ViewModel();
        }
        public async void Run(IBackgroundTaskInstance taskInstance)
        {
            // Get a deferral, to prevent the task from closing prematurely 
            // while asynchronous code is still running.
            BackgroundTaskDeferral deferral = taskInstance.GetDeferral();


            /*
            //Get app data
            int chargeHours = 2; //default
            int checkHourStart = 21;//default
            int checkHourEnd = 22;

            var localSettings = ApplicationData.Current.LocalSettings;
            if (localSettings.Values.ContainsKey("ChargeHours"))
                chargeHours = Convert.ToInt32(localSettings.Values["ChargeHours"]);
            if (localSettings.Values.ContainsKey("CheckHourStart"))
                checkHourStart = Convert.ToInt32(localSettings.Values["CheckHourStart"]);
            if (localSettings.Values.ContainsKey("CheckHourEnd"))
                checkHourEnd = Convert.ToInt32(localSettings.Values["CheckHourEnd"]);
             * */

            await vm.GetSensorData("0");
            await vm.GetEventData("2","O");
            SensorData sensorData = vm.GetLastSensorRead();
            EventData eventData = vm.GetLastEvent();
            if (sensorData != null)
            {
                //Update tile
                UpdateTile(sensorData.FormattedTemperature, sensorData.DeviceDateTime.ToString(),eventData.FormattedEventCode,eventData.DeviceDateTime.ToString());
            }

            /*
            EventData eventData = await CallEventWebAPI("http://sensors.cloudapp.net/Event/LastEvent/O/2");
            if (eventData != null)
            {
                //Update tile
                UpdateTile2(eventData.EventCode, eventData.DeviceDateTime);
            }
             * */

            /*
            //check for last charge at a certain time
            TimeSpan start = new TimeSpan(checkHourStart, 0, 0); 
            TimeSpan end = new TimeSpan(checkHourEnd, 0, 0); 
            TimeSpan now = DateTime.Now.TimeOfDay;
            if ((now >= start) && (now <= end))
            {
                var lastChargeData = await CallWebAPI("http://sensors.cloudapp.net/Sensor/LastCharge");
                if (lastChargeData != null)
                {
                    //See if last n hours
                    DateTime lastChargeDateTime = DateTime.Parse(lastChargeData.DbDateTime);
                    System.TimeSpan diff = DateTime.Now.Subtract(lastChargeDateTime);
                    if (diff.Hours >= chargeHours)
                    {
                        //Toast
                        Toast("Last Leaf Charge: ", lastChargeDateTime.ToString());
                    }
                }
            }
             * */

            // Inform the system that the task is finished.
            deferral.Complete();
        }

        private static void UpdateTile(string temperature,string sensorDatetime,string eventCode,string eventDateTime)
        {
            TileUpdateManager.CreateTileUpdaterForApplication().Clear();
            TileUpdateManager.CreateTileUpdaterForApplication().EnableNotificationQueue(true);

            // Create a notification for the Square150x150 tile using one of the available templates for the size.
            ITileSquare150x150Text01 square150x150Content = TileContentFactory.CreateTileSquare150x150Text01();
            square150x150Content.TextHeading.Text = temperature;
            square150x150Content.TextBody1.Text = sensorDatetime;

            // Send the notification to the application? tile.
            TileUpdateManager.CreateTileUpdaterForApplication().Update(square150x150Content.CreateNotification());

            // Create a notification for the Square150x150 tile using one of the available templates for the size.
            //square150x150Content = TileContentFactory.CreateTileSquare150x150Text01();
            square150x150Content.TextHeading.Text = eventCode;
            square150x150Content.TextBody1.Text = eventDateTime;

            // Send the notification to the application? tile.
            TileUpdateManager.CreateTileUpdaterForApplication().Update(square150x150Content.CreateNotification());
        }

        private void Toast(string heading,string body)
        {
            // Using the ToastText02 toast template.
            ToastTemplateType toastTemplate = ToastTemplateType.ToastText02;

            // Retrieve the content part of the toast so we can change the text.
            XmlDocument toastXml = ToastNotificationManager.GetTemplateContent(toastTemplate);

            //Find the text component of the content
            XmlNodeList toastTextElements = toastXml.GetElementsByTagName("text");

            // Set the text on the toast. 
            // The first line of text in the ToastText02 template is treated as header text, and will be bold.
            toastTextElements[0].AppendChild(toastXml.CreateTextNode(heading));
            toastTextElements[1].AppendChild(toastXml.CreateTextNode(body));

            // Set the duration on the toast
            IXmlNode toastNode = toastXml.SelectSingleNode("/toast");
            ((XmlElement)toastNode).SetAttribute("duration", "long");

            // Create the actual toast object using this toast specification.
            ToastNotification toast = new ToastNotification(toastXml);

            // Set SuppressPopup = true on the toast in order to send it directly to action center without 
            // producing a popup on the user's phone.
            toast.SuppressPopup = false;

            // Send the toast.
            ToastNotificationManager.CreateToastNotifier().Show(toast);
        }
    }
}