using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.AspNet.Mvc;
using System.IdentityModel.Tokens;
using System.Security.Claims;
using Microsoft.AspNet.Authorization;
using System.Security.Principal;
using Microsoft.AspNet.Authentication.JwtBearer;
using System.IdentityModel.Tokens.Jwt;
using Microsoft.WindowsAzure.Storage;
using Microsoft.WindowsAzure.Storage.Queue;
using Microsoft.Extensions.Configuration;
using WilkieHomeAutomation.Models;
using Newtonsoft.Json;

namespace WilkieHomeAutomation.Controllers
{
    [Route("api/[controller]")]
    public class EventsController : Controller
    {
        private EventContext _context;
        private readonly IConfiguration config;

        public EventsController(EventContext context, IConfiguration config)
        {
            _context = context;
            this.config = config;
        }

        // GET: api/events
        [HttpGet]
        //[Authorize("Bearer")]
        public IEnumerable<object> Get()
        {
            var query = (
                from Event in _context.Events
                from Device in _context.Devices
                    .Where(d => d.UnitNum == Event.UnitNum)
                    .DefaultIfEmpty()
                select new { Event = Event, Device = Device })
                    .OrderByDescending(e => e.Event.DeviceDate)  
                    .Take(10)
                    .AsEnumerable();

            return query;
        }

        // GET api/events/5
        [HttpGet("{unitNum}", Name = "GetEvent")]
        [Authorize("Bearer")]
        public IActionResult GetById(int unitNum)
        {
            Event _event = _context.Events.SingleOrDefault(b => b.UnitNum == unitNum);
            if (_event == null)
            {
                return this.HttpNotFound();
            }

            return this.Ok(_event);
        }

        // POST api/events
        [HttpPost]
        [Authorize("Bearer")]
        public IActionResult Insert([FromBody]Event _event)
        {
            if (ModelState.IsValid)
            {
                _context.Events.Add(_event);

                //Update device table
                Device dbDevice = _context.Devices.SingleOrDefault(b => b.UnitNum == _event.UnitNum);
                if (dbDevice != null)
                {
                    dbDevice.LastEventDT = _event.DeviceDate;
                }
                _context.SaveChanges();
            }

            //Grab the queue
            var storageAccount = CloudStorageAccount.Parse(config["Data:DefaultConnection:StorageConnectionString"]);
            CloudQueueClient queueClient = storageAccount.CreateCloudQueueClient();
            var queue = queueClient.GetQueueReference("humanevents");

            //Add message to queue for processing from human event job
            string queueMessageString = JsonConvert.SerializeObject(_event, Formatting.Indented); ;
            var queueMessage = new CloudQueueMessage(queueMessageString);
            queue.AddMessageAsync(queueMessage);

            return this.CreatedAtRoute("GetEvent", new { controller = "Events", unitNum = _event.UnitNum }, _event);
        }
    }
}
