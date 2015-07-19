using System;
using System.Net.Http;
using System.Web.Http;
using System.Net.Http.Headers;
using System.Web.Http.Cors;

namespace WorkerRole1
{
    [EnableCors(origins: "*", headers: "*", methods: "*")]
    public class EventController : ApiController
    {
        [HttpGet]
        [ActionName("EventList")]
        public HttpResponseMessage GetLastEvent(char eventType, int id)
        {
            Events eventInstance = new Events();
            string eventData = eventInstance.GetEventList(eventType, id);

            var resp = new HttpResponseMessage()
            {
                Content = new StringContent(eventData)
            };
            resp.Content.Headers.ContentType = new MediaTypeHeaderValue("application/json");
            return resp;
        }

        [HttpGet]
        [ActionName("EventList")]
        public HttpResponseMessage GetLastEvent(char eventType)
        {
            Events eventInstance = new Events();
            string eventData = eventInstance.GetEventList(eventType);

            var resp = new HttpResponseMessage()
            {
                Content = new StringContent(eventData)
            };
            resp.Content.Headers.ContentType = new MediaTypeHeaderValue("application/json");
            return resp;
        }

        [HttpGet]
        [ActionName("EventList")]
        public HttpResponseMessage GetLastEvent()
        {
            Events eventInstance = new Events();
            string eventData = eventInstance.GetEventList();

            var resp = new HttpResponseMessage()
            {
                Content = new StringContent(eventData)
            };
            resp.Content.Headers.ContentType = new MediaTypeHeaderValue("application/json");
            return resp;
        }
    }
}
