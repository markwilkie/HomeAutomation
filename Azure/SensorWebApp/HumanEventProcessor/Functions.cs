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
       // public static void ProcessQueueMessage([QueueTrigger("queue")] string message, TextWriter log)
        //{
       //     log.WriteLine(message);
        //}

        //HTTP function
        [NoAutomaticTrigger]
        public static void Test(string input, IBinder binder)
        {
            Console.WriteLine("HTTP function: " + input);

            //dynamic d = JObject.Parse(input);

            //string blobPath = string.Format("test/{0}", d.id);
            //TextWriter writer = binder.Bind<TextWriter>(new BlobAttribute(blobPath));
            //writer.WriteLine(d.data);
        }

        // Runs once every 30 seconds
        public static void TimerJob([TimerTrigger("00:00:30")] TimerInfo timer)
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
