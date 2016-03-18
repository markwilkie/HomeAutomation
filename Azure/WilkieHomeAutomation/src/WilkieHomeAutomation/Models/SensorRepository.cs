using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Linq;
using System.Threading.Tasks;

namespace WilkieHomeAutomation.Models
{
    public class SensorRepository : ISensorRepository
    {
        private readonly ConcurrentDictionary<int, Sensor> sensors;

        public SensorRepository()
        {
            this.sensors = new ConcurrentDictionary<int, Sensor>();
        }

        public void Add(Sensor sensor)
        {
            this.sensors.TryAdd(sensor.UnitNum, sensor);
        }

        public Sensor Find(int unitNum)
        {
            Sensor sensor;
            this.sensors.TryGetValue(unitNum, out sensor);
            return sensor;
        }

        public IEnumerable<Sensor> GetAll()
        {
            return this.sensors.Values.OrderBy(b => b.UnitNum);
        }

        public Sensor Remove(int unitNum)
        {
            Sensor sensor;
            this.sensors.TryRemove(unitNum, out sensor);
            return sensor;
        }

        public void Update(Sensor sensor)
        {
            this.sensors[sensor.UnitNum] = sensor;
        }
    }
}
