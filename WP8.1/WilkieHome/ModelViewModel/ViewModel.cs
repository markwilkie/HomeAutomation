using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Collections.ObjectModel;
using System.Threading.Tasks;
using System.Net.Http;
using System.Net.Http.Headers;
using Newtonsoft.Json;
using WilkieHome.Model;

namespace WilkieHome.VM
{
    public class ViewModel
    {
        public List<SensorData> sensorDataList { get; set; }
        public List<EventData> eventDataList { get; set; }
        public SensorData sensorData { get; set; }
        public EventData eventData { get; set; }

        public async Task<List<SensorData>> GetSensorData()
        {
            return await GetSensorData(null);
        }
         public async Task<List<SensorData>> GetSensorData(string deviceNumber)
         {
             string uri = "http://sensors.cloudapp.net/Sensor/SensorList/";
             if (deviceNumber != null)
             {
                 uri = uri + deviceNumber;
             }

            HttpClient client = new HttpClient();

            client.BaseAddress = new Uri(uri);
            client.DefaultRequestHeaders.Accept.Clear();
            client.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
            var response = await client.GetAsync(uri);
            if (response.IsSuccessStatusCode)
            {
                var responseText = await response.Content.ReadAsStringAsync();
                sensorDataList = JsonConvert.DeserializeObject<List<SensorData>>(responseText);
            }

            sensorData = sensorDataList.First();
            return sensorDataList;
         }

         public SensorData GetLastSensorRead()
         {
             if (sensorDataList != null)
                 return sensorDataList.First();
             else
                 return null;
         }

         public async Task<List<EventData>> GetEventData()
         {
             return await GetEventData(null,null);
         }
         public async Task<List<EventData>> GetEventData(string deviceNumber,string eventType)
         {
             string uri = "http://sensors.cloudapp.net/Event/EventList/";
             if (eventType != null && deviceNumber != null)
             {
                 uri = uri + eventType + "/" + deviceNumber; ;
             } 
             HttpClient client = new HttpClient();

             client.BaseAddress = new Uri(uri);
             client.DefaultRequestHeaders.Accept.Clear();
             client.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
             var response = await client.GetAsync(uri);
             if (response.IsSuccessStatusCode)
             {

                 var responseText = await response.Content.ReadAsStringAsync();
                 eventDataList = JsonConvert.DeserializeObject<List<EventData>>(responseText);
                 //List<EventData> sortedEventDataList = eventDataList.OrderByDescending(x => x.DeviceDateTime).ToList();
             }

             eventData = eventDataList.First();
             return eventDataList;
         }

         public EventData GetLastEvent()
         {
             if (eventDataList != null)
                 return eventDataList.First();
             else
                 return null;
         }
    }
}
