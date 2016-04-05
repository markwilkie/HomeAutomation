using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Data.SqlClient;
using System.Data;
using System.Threading.Tasks;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.IO;
using Newtonsoft.Json;
using System.Diagnostics;

namespace WorkerRole1
{
    class Sensors
    {
        SqlConnection sqlConnection;

        public Sensors()
        {
            try
            {
                //Open SQL connection
                string sqlConnectionString = "Data Source=x8pxlhcfpy.database.windows.net;Initial Catalog=sensors;User ID=mwilkie;pwd=1234Data!";
                sqlConnection = new SqlConnection(sqlConnectionString);
                sqlConnection.Open();
            }
            catch (Exception e) 
            {
                throw e;
            }
        }

        public string GetLastSensor(int id)
        {
            return GetSensorList(id,1);
        }

        public string GetSensorList()
        {
            string sqlCommand = String.Format("select top {0} unitnum, vcc, temperature, intpinstate, devicedatetime from state order by devicedatetime desc", 10);
            string sensorJSon = SerializeSqlData(sqlCommand);
            return sensorJSon;
        }

        public string GetSensorList(int id,int rows)
        {
            string sqlCommand = String.Format("select top {1} unitnum, vcc, temperature, intpinstate, devicedatetime from state where unitnum = {0} order by devicedatetime desc", id, rows);
            string sensorJSon = SerializeSqlData(sqlCommand);
            return sensorJSon;
        }

        public string GetLastEvent(char eventType, int id)
        {
            string sqlCommand = String.Format("select top 1 UnitNum,EventCodeType,EventCode,DeviceDateTime from event where eventcodetype = '{1}' and unitnum = {0} order by devicedatetime desc", id, eventType);
            string eventJSon = SerializeSqlData(sqlCommand);
            return eventJSon;
        }

        public string SerializeSqlData(string sqlCommmand)
        {
            string sqlJSonData = "unknown";

            //Get data from database
            SqlCommand command = new SqlCommand(sqlCommmand,sqlConnection);
            SqlDataReader reader = command.ExecuteReader();

            //Load new datatable from reader
            DataTable dataTable = new DataTable(); 
            dataTable.Load(reader);
            reader.Close();  //Close SQL

            //Serialize dataset to JSon
            sqlJSonData = JsonConvert.SerializeObject(dataTable);

            return sqlJSonData;
        }
    }
}
