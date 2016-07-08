using Microsoft.Data.Entity;

namespace WilkieHomeAutomation.Models
{
    public class ResolvedEventContext : DbContext
    {
        public DbSet<ResolvedEvent> ResolvedEvents { get; set; }

        protected override void OnModelCreating(ModelBuilder modelBuilder)
        {
            modelBuilder.Entity<ResolvedEvent>().Ignore(t => t.DeviceDateTimeStr);
        }
    }
}
