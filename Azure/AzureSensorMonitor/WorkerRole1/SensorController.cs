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

            /*
            return new HttpResponseMessage()
            {
                Content = new StringContent(temperatureData)
            };
             * */

            var resp = new HttpResponseMessage()
            {
                Content = new StringContent(temperatureData)
            };
            resp.Content.Headers.ContentType = new MediaTypeHeaderValue("application/json");
            return resp;
        }
    }
}
