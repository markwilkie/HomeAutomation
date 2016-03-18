using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace WilkieHomeAutomation.Models
{
    public interface ISensorRepository
    {
        void Add(Sensor sensor);
        IEnumerable<Sensor> GetAll();
        Sensor Find(int unitNum);
        Sensor Remove(int unitNum);
        void Update(Sensor sensor);
    }
}
