using System;
using System.Net.Http;
using System.Web.Http;
using System.Net.Http.Headers;

namespace WorkerRole1
{
    public class SensorController : ApiController
    {
        [HttpGet]
        [ActionName("SensorList")]
        public HttpResponseMessage GetSensorList()
        {
            Sensors sensor = new Sensors();
            string sensorData = sensor.GetSensorList();

            var resp = new HttpResponseMessage()
            {
                Content = new StringContent(sensorData)
            };
            resp.Content.Headers.ContentType = new MediaTypeHeaderValue("application/json");
            return resp;
        }

        [HttpGet]
        [ActionName("SensorList")]
        public HttpResponseMessage GetSensorList(int id)
        {
            return GetSensorList(id, 10);
        }

        [HttpGet]
        [ActionName("SensorList")]
        public HttpResponseMessage GetSensorList(int id,int rows)
        {
            Sensors sensor = new Sensors();
            string sensorData = sensor.GetSensorList(id, rows);

            var resp = new HttpResponseMessage()
            {
                Content = new StringContent(sensorData)
            };
            resp.Content.Headers.ContentType = new MediaTypeHeaderValue("application/json");
            return resp;
        }

        [HttpGet]
        [ActionName("LastSensor")]
        public HttpResponseMessage GetLastSensor()
        {
            return GetLastSensor(0);
        }

        [HttpGet]
        [ActionName("LastSensor")]
        public HttpResponseMessage GetLastSensor(int id)
        {
            Sensors sensor = new Sensors();
            string sensorData = sensor.GetLastSensor(id);

            var resp = new HttpResponseMessage()
            {
                Content = new StringContent(sensorData)
            };
            resp.Content.Headers.ContentType = new MediaTypeHeaderValue("application/json");
            return resp;
        }
    }
}
