using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.Data.Entity;

namespace WilkieHomeAutomation.Models
{
    public class EventContext: DbContext
    {
        public DbSet<Event> Events { get; set; }
        public DbSet<Device> Devices { get; set; }

        protected override void OnModelCreating(ModelBuilder modelBuilder)
        {
            modelBuilder.Entity<Device>().Ignore(t => t.DeviceDateTimeStr);
            modelBuilder.Entity<Device>().Ignore(t => t.LastState);
            modelBuilder.Entity<Device>().Ignore(t => t.LastEvent);

            modelBuilder.Entity<Event>().Ignore(e => e.EventTypeDesc);
            modelBuilder.Entity<Event>().Ignore(e => e.EventDesc);
        }
    }
}
