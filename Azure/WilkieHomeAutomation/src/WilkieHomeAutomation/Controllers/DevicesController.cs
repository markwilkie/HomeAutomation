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
    public class DevicesController : Controller
    {
        private DeviceContext _context;

        public DevicesController(DeviceContext context)
        {
            _context = context;
        }

        // GET: api/devices
        [HttpGet]
        //[Authorize("Bearer")]
        public IEnumerable<object> Get()
        {
            var query = (
                from Device in _context.Devices
                from State in _context.States
                    .Where(s => s.UnitNum == Device.UnitNum && s.DeviceDate == Device.LastStateDT)
                    .DefaultIfEmpty()
                select new { Device = Device, State = State}).AsEnumerable();

            return query;
        }

        // GET api/devices/5
        [HttpGet("{unitNum}", Name = "GetDevice")]
        //[Authorize("Bearer")]
        public IActionResult GetById(int unitNum)
        {
            Device device = _context.Devices.SingleOrDefault(b => b.UnitNum == unitNum);
            if (device == null)
            {
                return this.HttpNotFound();
            }

            return this.Ok(device);
        }

        // POST api/devices
        [HttpPost]
        [Authorize("Bearer")]
        public IActionResult InsertUpdate([FromBody]Device device)
        {
            Device dbDevice = _context.Devices.SingleOrDefault(b => b.UnitNum == device.UnitNum);
            if (dbDevice == null)
            {
                if (ModelState.IsValid)
                {
                    _context.Devices.Add(device);
                    _context.SaveChanges();
                }
            }
            else
            {
                if (ModelState.IsValid)
                {
                    dbDevice.Description = device.Description;
                    dbDevice.DeviceDate = device.DeviceDate;
                    _context.SaveChanges();
                }
            }

            return this.CreatedAtRoute("GetDevice", new { controller = "Devices", unitNum = device.UnitNum }, device);
        }
    }
}
