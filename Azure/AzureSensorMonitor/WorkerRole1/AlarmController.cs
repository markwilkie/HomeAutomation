using System;
using System.Net;
using System.Net.Http;
using System.Web.Http;
using System.Net.Http.Headers;
using System.Web.Http.Cors;

namespace WorkerRole1
{
    [EnableCors(origins: "*", headers: "*", methods: "*")]
    public class AlarmController : ApiController
    {
        [HttpGet]
        [ActionName("SendAlarm")]
        public HttpResponseMessage GetSendAlarm(char alarmType, char alarmCode)
        {
            return null;
        }
    }
}
