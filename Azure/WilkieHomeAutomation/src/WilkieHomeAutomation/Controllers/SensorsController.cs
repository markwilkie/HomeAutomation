using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.AspNet.Mvc;
using WilkieHomeAutomation.Models;

namespace WilkieHomeAutomation.Controllers
{
    [Route("api/[controller]")]
    public class SensorsController : Controller
    {
        private readonly ISensorRepository sensors;

        public SensorsController(ISensorRepository sensors)
        {
            this.sensors=sensors;
        }

        // GET: api/sensors
        [HttpGet]
        public IEnumerable<Sensor> Get()
        {
            return this.sensors.GetAll();
        }

        // GET api/sensors/5
        [HttpGet("{unitNum}", Name = "GetSensor")]
        public IActionResult GetById(int unitNum)
        {
            var sensor = this.sensors.Find(unitNum);
            if (sensor == null)
            {
                return this.HttpNotFound();
            }

            return this.Ok(sensor);
        }

        // POST api/sensors
        [HttpPost]
        public IActionResult Create([FromBody]Sensor sensor)
        {
            this.sensors.Add(sensor);

            return this.CreatedAtRoute("GetSensor", new { controller = "Sensors", unitNum = sensor.UnitNum }, sensor);
        }

        // PUT api/sensors/5
        [HttpPut("{unitNum}")]
        public IActionResult Update(int unitNum, [FromBody]Sensor sensor)
        {
            if (sensor.UnitNum != unitNum)
            {
                return this.HttpBadRequest();
            }

            var existingSensor = this.sensors.Find(unitNum);
            if (existingSensor == null)
            {
                return this.HttpNotFound();
            }

            this.sensors.Update(sensor);

            /*
            this.logger.LogVerbose(
                "Updated {0} by {1} to {2} by {3}",
                existingBook.Title,
                existingBook.Author,
                book.Title,
                book.Author);
                */

            return new NoContentResult();
        }

        // DELETE api/sensors/5
        [HttpDelete("{unitNum}")]
        public NoContentResult Delete(int unitNum)
        {
            this.sensors.Remove(unitNum);
            return new NoContentResult();
        }
    }
}
