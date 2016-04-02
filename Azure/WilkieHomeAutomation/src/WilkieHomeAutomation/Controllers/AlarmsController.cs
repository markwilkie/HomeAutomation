using System;
using System.Linq;
using System.Net.Sockets;
using Microsoft.AspNet.Mvc;
using System.Security.Claims;
using Microsoft.AspNet.Authorization;
using System.Security.Principal;
using Microsoft.AspNet.Authentication.JwtBearer;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using MailKit.Net.Smtp;
using MailKit.Security;
using MimeKit;

using WilkieHomeAutomation.Models;


namespace WilkieHomeAutomation.Controllers
    {
        [Route("api/[controller]")]
        public class AlarmsController : Controller
        {
            private readonly ILogger<AlarmsController> logger;
            private readonly IConfiguration config;

            public AlarmsController(ILogger<AlarmsController> logger, IConfiguration config)
            {
                this.logger = logger;
                this.config = config;
            }
        
            /// <summary>
            /// Sound the alarm
            /// </summary>
            /// <param name="req"></param>
            /// <returns></returns>
            [HttpPost]
            [Authorize("Bearer")]
            public IActionResult Post([FromBody] Alarm alarm)
            {
            var m = new MimeMessage();
            m.From.Add(new MailboxAddress("", config["Data:Mail:From"]));
            m.To.Add(new MailboxAddress("", config["Data:Mail:To"]));
            m.Subject = "Home ALARM";

            BodyBuilder bodyBuilder = new BodyBuilder();
            bodyBuilder.TextBody = "Unit: " + alarm.UnitNum + " Alarm Type: " + alarm.AlarmType + " and Code: " + alarm.AlarmCode;
            m.Body = bodyBuilder.ToMessageBody();

            using (var smtpClient = new SmtpClient())
            {
                smtpClient.Connect("smtp.outlook.office365.com",587, false); //host, port, usessl
                 smtpClient.AuthenticationMechanisms.Remove("XOAUTH2"); // Note: since we don't have an OAuth2 token, disable
                smtpClient.Authenticate(config["Data:Mail:SMTPUserName"], config["Data:Mail:SMTPPass"]);
                smtpClient.Send(m);
                smtpClient.Disconnect(true);
            }

            return this.Ok();
            }
        }
    }
