using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.Data.Entity;

namespace WilkieHomeAutomation.Models
{
    public class StateContext: DbContext
    {
        public DbSet<State> States { get; set; }
        public DbSet<Device> Devices { get; set; }

        protected override void OnModelCreating(ModelBuilder modelBuilder)
        {
            modelBuilder.Entity<Device>().Ignore(t => t.DeviceDateTimeStr);
            modelBuilder.Entity<Device>().Ignore(t => t.LastWeather);
            modelBuilder.Entity<Device>().Ignore(t => t.LastState);
            modelBuilder.Entity<Device>().Ignore(t => t.LastEvent);
        }
    }
}
