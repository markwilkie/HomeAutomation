using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.AspNet.Mvc;
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
        public IEnumerable<Device> Get()
        {
            return _context.Devices.AsEnumerable();
        }

        // GET api/devices/5
        [HttpGet("{unitNum}", Name = "GetDevice")]
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
        public IActionResult Create([FromBody]Device device)
        {
            if (ModelState.IsValid)
            {
                _context.Devices.Add(device);
                _context.SaveChanges();
            }

            return this.CreatedAtRoute("GetDevice", new { controller = "Devices", unitNum = device.UnitNum }, device);
        }
    }
}
