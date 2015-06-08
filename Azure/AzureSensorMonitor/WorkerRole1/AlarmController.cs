using System;
using System.Net;
using System.Net.Http;
using System.Web.Http;
using System.Net.Http.Headers;
using Twilio;

namespace WorkerRole1
{
    public class AlarmController : ApiController
    {
        [HttpGet]
        [ActionName("SendAlarm")]
        public HttpResponseMessage GetSendAlarm(char alarmType, char alarmCode)
        {
            // Find your Account Sid and Auth Token at twilio.com/user/account 
            string AccountSid = "ACb0bea1ed9ee68ab139f5e07d9d8f2c5b";
            string AuthToken = "";
            var twilioClient = new TwilioRestClient(AccountSid, AuthToken);
            var sms = twilioClient.SendSmsMessage("+15005550006", "+12063311936", "ALARM", "");
            //"ALARM!\\n Type:" + alarmType + " Code: "
            if (sms.RestException != null)
            {
                var error = sms.RestException.Message;
            }

            var resp = new HttpResponseMessage(HttpStatusCode.OK);
            //resp.Content.Headers.ContentType = new MediaTypeHeaderValue("application/json");
            return resp;
        }
    }
}
