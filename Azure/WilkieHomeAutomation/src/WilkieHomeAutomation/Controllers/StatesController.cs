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
    public class StatesController : Controller
    {
        private StateContext _context;

        public StatesController(StateContext context)
        {
            _context = context;
        }

        // GET: api/states
        [HttpGet]
        [Authorize("Bearer")]
        public IEnumerable<State> Get()
        {
            return _context.States.AsEnumerable();
        }

        // GET api/states/5
        [HttpGet("{unitNum}", Name = "GetState")]
        [Authorize("Bearer")]
        public IActionResult GetById(int unitNum)
        {
            State state = _context.States.SingleOrDefault(b => b.UnitNum == unitNum);
            if (state == null)
            {
                return this.HttpNotFound();
            }

            return this.Ok(state);
        }

        // POST api/states
        [HttpPost]
        [Authorize("Bearer")]
        public IActionResult Insert([FromBody]State state)
        {
            if (ModelState.IsValid)
            {
                //Add to state table
                _context.States.Add(state);

                //Update device table
                Device dbDevice = _context.Devices.SingleOrDefault(b => b.UnitNum == state.UnitNum);
                if (dbDevice != null)
                {
                    dbDevice.LastStateDT = state.DeviceDate;
                }

                _context.SaveChanges();
            }

            return this.CreatedAtRoute("GetState", new { controller = "States", unitNum = state.UnitNum }, state);
        }
    }
}
