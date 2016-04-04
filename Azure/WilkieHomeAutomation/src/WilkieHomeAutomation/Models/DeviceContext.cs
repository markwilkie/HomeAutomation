using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.Data.Entity;
using Microsoft.Extensions.Configuration;

namespace WilkieHomeAutomation.Models
{
    public class DeviceContext: DbContext
    {
        public DbSet<Device> Devices { get; set; }
    }
}
