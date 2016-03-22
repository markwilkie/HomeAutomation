using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace WilkieHomeAutomation.Models
{
    public class Device
    {
        //private DateTime dateAdded;  //backing field, named by convention

        public int ID { get; set; }
        public int UnitNum { get; set; }
        public string Description { get; set; }
        /*
        public DateTime DateAdded 
        {
            get { return dateAdded;  }
            set { dateAdded = DateTime.Now; }
        }
        */
    }
}
