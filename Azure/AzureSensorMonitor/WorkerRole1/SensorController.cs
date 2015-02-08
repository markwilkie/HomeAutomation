using System;
using System.Net.Http;
using System.Web.Http;
using System.Net.Http.Headers;

namespace WorkerRole1
{
    public class SensorController : ApiController
    {
        [HttpGet]
        [ActionName("CurrentTemperature")]
        public HttpResponseMessage GetCurrentTemperature()
        {
            return GetCurrentTemperature(0);
        }

        [HttpGet]
        [ActionName("CurrentTemperature")]
        public HttpResponseMessage GetCurrentTemperature(int id)
        {
            Sensors sensorData = new Sensors();
            string temperatureData = sensorData.GetCurrentTemperature(id);

            var resp = new HttpResponseMessage()
            {
                Content = new StringContent(temperatureData)
            };
            resp.Content.Headers.ContentType = new MediaTypeHeaderValue("application/json");
            return resp;
        }

        [HttpGet]
        [ActionName("LastCharge")]
        public HttpResponseMessage GetLastCharge()
        {
            Sensors sensorData = new Sensors();
            string lastCharge = sensorData.GetLastCharge();

            var resp = new HttpResponseMessage()
            {
                Content = new StringContent(lastCharge)
            };
            resp.Content.Headers.ContentType = new MediaTypeHeaderValue("application/json");
            return resp;
        }
    }
}
