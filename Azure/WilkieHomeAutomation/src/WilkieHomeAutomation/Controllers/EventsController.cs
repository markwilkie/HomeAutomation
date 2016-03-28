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
        [Authorize("Bearer")]
        public IEnumerable<Event> Get()
        {
            return _context.Events.AsEnumerable();
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
                _context.SaveChanges();
            }

            return this.CreatedAtRoute("GetEvent", new { controller = "Events", unitNum = _event.UnitNum }, _event);
        }
    }
}
