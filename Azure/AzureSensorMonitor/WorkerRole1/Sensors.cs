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

namespace WorkerRole1
{
    class Sensors
    {
        SqlConnection sqlConnection;

        public Sensors()
        {
            try
            {
                //SQL
                string sqlConnectionString = "Data Source=x8pxlhcfpy.database.windows.net;Initial Catalog=sensors;User ID=mwilkie;pwd=1234Data!";
                sqlConnection = new SqlConnection(sqlConnectionString);
                sqlConnection.Open();
            }
            catch (Exception e) 
            {
                throw e;
            }
        }

        public string GetCurrentTemperature(int id)
        {
            string currentTemp = "unknown";

            //Now that we've woken up, let's read database
            String sqlCommmand = String.Format("select top 1 DeviceName,DeviceData1,DbDateTime from sensordata where devicename = 'Therm{0}' order by id desc",id); 
            SqlCommand command = new SqlCommand(sqlCommmand,sqlConnection);
            SqlDataReader reader = command.ExecuteReader();

            while (reader.Read())  //loop through rows
            {
                IDataRecord record = (IDataRecord)reader;

                //Calc and round temp
                double flTemp=(Convert.ToDouble(record[1]) * ((double)9 / (double)5)) + 32;

                //Time to serialize our data to json now
                SensorData sensorData = new SensorData();
                sensorData.DeviceName = (string)record[0];
                sensorData.DeviceData1 = (int)Math.Round(flTemp, MidpointRounding.AwayFromZero);
                sensorData.DbDateTime = (string)record[2].ToString();

                currentTemp = SerializeJSon<SensorData>(sensorData);
            }

            //Close up sql connection
            reader.Close();

            return currentTemp;
        }

        public string GetLastCharge()
        {
            string lastCharge = "unknown";

            //Now that we've woken up, let's read database
            String sqlCommmand = "select top 1 DeviceName,DeviceData1,DbDateTime from sensordata where devicename = 'power2' and cast(devicedata1 as float) > 10 order by id desc";
            SqlCommand command = new SqlCommand(sqlCommmand, sqlConnection);
            SqlDataReader reader = command.ExecuteReader();

            while (reader.Read())  //loop through rows
            {
                IDataRecord record = (IDataRecord)reader;

                //Calc and round temp
                double flTemp = Convert.ToDouble(record[1]);

                //Time to serialize our data to json now
                SensorData sensorData = new SensorData();
                sensorData.DeviceName = (string)record[0];
                sensorData.DeviceData1 = (int)Math.Round(flTemp, MidpointRounding.AwayFromZero);
                sensorData.DbDateTime = (string)record[2].ToString();

                lastCharge = SerializeJSon<SensorData>(sensorData);
            }

            //Close up sql connection
            reader.Close();

            return lastCharge;
        }
        public static string SerializeJSon<T>(T t)
        {
            MemoryStream stream = new MemoryStream();
            DataContractJsonSerializer ds = new DataContractJsonSerializer(typeof(T));
            DataContractJsonSerializerSettings s = new DataContractJsonSerializerSettings();
            ds.WriteObject(stream, t);
            string jsonString = Encoding.UTF8.GetString(stream.ToArray());
            stream.Close();
            return jsonString;
        }
    }

    [DataContract]
    class SensorData
    {
        [DataMember]
        public string DeviceName { get; set; }
        [DataMember]
        public double DeviceData1 { get; set; }
        [DataMember]
        public double DeviceData2 { get; set; }
        [DataMember]
        public string DbDateTime { get; set; }
    }
}
