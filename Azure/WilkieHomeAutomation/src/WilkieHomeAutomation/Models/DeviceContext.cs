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
        public DbSet<State> States { get; set; }
        public DbSet<Event> Events { get; set; }

        protected override void OnModelCreating(ModelBuilder modelBuilder)
        {
            modelBuilder.Entity<Device>().Ignore(t => t.DeviceDateTimeStr);
            modelBuilder.Entity<Device>().Ignore(t => t.LastWeather);
            modelBuilder.Entity<Device>().Ignore(t => t.LastState);
            modelBuilder.Entity<Device>().Ignore(t => t.LastEvent);
        }
    }
}
