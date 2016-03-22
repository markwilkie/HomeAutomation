using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.Data.Entity;

namespace WilkieHomeAutomation.Models
{
    public class DeviceContext: DbContext
    {
        public DbSet<Device> Devices { get; set; }
     }
}
