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
    public class WeatherController : Controller
    {
        private WeatherContext _context;

        public WeatherController(WeatherContext context)
        {
            _context = context;
        }

        // GET: api/weather
        [HttpGet]
        //[Authorize("Bearer")]
        public IEnumerable<Weather> Get()
        {
            return _context.Readings.AsEnumerable();
        }

        // GET api/weather/5
        [HttpGet("{unitNum}", Name = "GetWeather")]
        [Authorize("Bearer")]
        public IActionResult GetById(int unitNum)
        {
            Weather reading = _context.Readings.SingleOrDefault(b => b.UnitNum == unitNum);
            if (reading == null)
            {
                return this.HttpNotFound();
            }

            return this.Ok(reading);
        }

        // POST api/weather
        [HttpPost]
        [Authorize("Bearer")]
        public IActionResult Insert([FromBody]Weather reading)
        {
            if (ModelState.IsValid)
            {
                //Add to state table
                _context.Readings.Add(reading);

                //Update device table
                Device dbDevice = _context.Devices.SingleOrDefault(b => b.UnitNum == reading.UnitNum);
                if (dbDevice != null)
                {
                    dbDevice.LastWeatherDT = reading.ReadingDate;
                }

                _context.SaveChanges();
            }

            return this.CreatedAtRoute("GetWeather", new { controller = "Weather", unitNum = reading.UnitNum }, reading);
        }
    }
}
