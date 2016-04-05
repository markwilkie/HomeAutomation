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
using WilkieHomeAutomation.Models;

namespace WilkieHomeAutomation.Controllers
{
    [Route("api/[controller]")]
    public class EventsController : Controller
    {
        private EventContext _context;

        public EventsController(EventContext context)
        {
            _context = context;
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

            return this.CreatedAtRoute("GetEvent", new { controller = "Events", unitNum = _event.UnitNum }, _event);
        }
    }
}
