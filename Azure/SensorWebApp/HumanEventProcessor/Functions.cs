using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Data;
using System.Data.SqlClient;
using System.Configuration;
using Microsoft.Azure.WebJobs;

namespace HumanEventProcessor
{
    public class Functions
    {
        // This function will get triggered/executed when a new message is written 
        // on an Azure Queue called queue.
        public static void ProcessQueueMessage([QueueTrigger("humanevents")] string message, TextWriter log)
        {
            log.WriteLine(message);
        }

        // Runs once every 10 minutes
        public static void TimerJob([TimerTrigger("00:10:00")] TimerInfo timer)
        {
            Console.WriteLine("Timer job fired!");

            using (SqlConnection connection = new SqlConnection(ConfigurationManager.ConnectionStrings["MyDBConnectionString"].ConnectionString))
            {
                connection.Open();

                string sql = "INSERT INTO HumanEvent(UnitNum,HumanEventType,HumanEventCode) VALUES(@param1,@param2,@param3)";
                SqlCommand cmd = new SqlCommand(sql, connection);
                cmd.Parameters.Add("@param1", SqlDbType.Int).Value = 99;
                cmd.Parameters.Add("@param2", SqlDbType.VarChar, 10).Value = "T";
                cmd.Parameters.Add("@param3", SqlDbType.VarChar, 10).Value = "HC";
                cmd.CommandType = CommandType.Text;
                //cmd.ExecuteNonQuery();

                /*
                SqlDataReader rd = cmd.ExecuteReader();
                DataTable dt = new DataTable();
                dt.Load(rd);
                Console.WriteLine("Load Data table from Reader");
                foreach (DataRow r in dt.Rows)
                {
                    Console.Write("ID: " + r["id"].ToString());
                    Console.Write("\n");
                }
                rd.Close();
                */

                connection.Close();
            }
        }
    }
}
