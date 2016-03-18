using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Linq;
using System.Threading.Tasks;

namespace WilkieHomeAutomation.Models
{
    public class SensorRepository : ISensorRepository
    {
        private readonly ConcurrentDictionary<string, Sensor> sensors;

        public SensorRepository()
        {
            this.sensors = new ConcurrentDictionary<string, Sensor>();
        }

        public void Add(Sensor sensor)
        {
            sensor.Key = Guid.NewGuid().ToString();
            this.sensors.TryAdd(sensor.Key, sensor);
        }

        public Sensor Find(string key)
        {
            Sensor sensor;
            this.sensors.TryGetValue(key, out sensor);
            return sensor;
        }

        public IEnumerable<Sensor> GetAll()
        {
            return this.sensors.Values.OrderBy(b => b.Num);
        }

        public Sensor Remove(string key)
        {
            Sensor sensor;
            this.sensors.TryRemove(key, out sensor);
            return sensor;
        }

        public void Update(Sensor sensor)
        {
            this.sensors[sensor.Key] = sensor;
        }
    }
}
