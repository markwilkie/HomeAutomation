using System;
using System.Net;
using System.Net.Http;
using System.Web.Http;
using System.Net.Http.Headers;
using System.Web.Http.Cors;
using System.Net.Mail;
using System.Net;
using System.IO; 

namespace WorkerRole1
{
    [EnableCors(origins: "*", headers: "*", methods: "*")]
    public class AlarmController : ApiController
    {
        [HttpGet]
        [ActionName("SendAlarm")]
        public HttpResponseMessage GetSendAlarm(int id, char alarmType, char alarmCode)
        {
            MailMessage mail = new MailMessage();
            mail.To.Add("????@tmomail.net");
            mail.From = new MailAddress("myemail");
            mail.Subject = "Home ALARM";
            mail.Body = "ID: " + id + " Alarm Type: " + alarmType + " and Code: " + alarmCode;
            mail.IsBodyHtml = false;

            try
            {
                SmtpClient smtpClient = new SmtpClient();
                smtpClient.Host = "smtp.outlook.office365.com";
                smtpClient.UseDefaultCredentials = false;
                smtpClient.Credentials = new System.Net.NetworkCredential("username", "pass");
                smtpClient.EnableSsl = true;
                smtpClient.Port = 587;
                smtpClient.DeliveryMethod = SmtpDeliveryMethod.Network;
                smtpClient.Send(mail);
            }
            catch (Exception e)
            {
                // Failed to send mail 
                var httpErrRes = new HttpResponseMessage(HttpStatusCode.InternalServerError);
                httpErrRes.Content = new StringContent(e.ToString());
                return httpErrRes;
            }
            finally
            {
                mail.Dispose();
            }

            var httpRes = new HttpResponseMessage(HttpStatusCode.OK);
            return httpRes;
        }
    }
}
