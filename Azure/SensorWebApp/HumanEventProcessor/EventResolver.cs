using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Newtonsoft.Json.Linq;
using System.Data;
using System.IO;
using System.Data.SqlClient;

namespace HumanEventProcessor
{
    class EventResolver
    {
        JObject jsonObject;
        string dbConnectionString;

        public EventResolver(string _dbConnectionString)
        {
            dbConnectionString = _dbConnectionString;
            jsonObject = JObject.Parse(GetJSON());
        }

        public void ResolveEventMsg(int unitNum,string eventCodeType, string eventCode, long deviceEpoch)
        {
            //Get first event (via Azure msg) and pass it in to see if there's a simple match in the json critera to get us started - then add new resolved event as appropriate
            JToken rootEventToken = jsonObject.First; //seed loop
            while (true)
            {
                JToken eventToken = jsonObject.SelectToken(rootEventToken.Path);
                JToken criteriaToken = jsonObject.SelectToken(eventToken.Path + ".Criteria");

                if (CheckIfSimpleMatch(criteriaToken, unitNum, eventCodeType, eventCode))
                {
                    //Since there's a match, we'll add a new resolved event
                    string tokenName = ((JProperty)rootEventToken).Name;
                    string resolution = (string)eventToken["Resolution"];
                    string confidence = (string)eventToken["Confidence"];
                    AddNewResolvedEvent(tokenName, resolution, confidence, tokenName, deviceEpoch);  //Added event to database
                    break;  //return since we've found a match
                }

                //Get next
                rootEventToken = rootEventToken.Next;
                if (rootEventToken == null)
                    break;
            }
        }

        public void ResolveDbEvents()
        {
            //Get list of open ResolvedEvents from db and interate through them
            DataTable eventDataTable = GetOpenEvents(); //900 sec = 15 minutes   - don't grab open events older than that
            foreach (DataRow eventRow in eventDataTable.Rows)
            {
                //Get path
                string path = eventRow["JSONPath"].ToString();
                long deviceEpoch = (long)eventRow["DeviceEpoch"];
                Console.WriteLine("Evaluating json path: " + path + " for Device Epoch: " + deviceEpoch);

                //Since we've got some to work on, let's grab the events
                DataTable rawEventDataTable = GetRawEvents();
                JToken eventToken = jsonObject.SelectToken(path).First;  //start where we left off
                while (eventToken != null)
                {
                    //is token actually an event?
                    if (((JProperty)eventToken).Name.StartsWith("EVENT_"))
                    {
                        Console.WriteLine("Evaluating event token: " + eventToken.Path);

                        JToken criteriaToken = jsonObject.SelectToken(eventToken.Path + ".Criteria");
                        if (CheckIfMatch(criteriaToken, deviceEpoch, rawEventDataTable))
                        {
                            string resolutionName = ((JProperty)eventToken).Name;
                            path = (string)eventToken.Path;
                            eventToken = jsonObject.SelectToken(path);
                            string resolution = (string)eventToken["Resolution"];
                            string confidence = (string)eventToken["Confidence"];
                            int id = (int)eventRow["Id"];

                            Console.WriteLine("Match!!  Resultion: " + resolution + " Confidence: " + confidence);

                            UpdateResolvedEvent(resolutionName, resolution, confidence, path, id);
                        }
                    }

                    //Get next event token 
                    eventToken = eventToken.Next;
                }
            }
        }

        private bool CheckIfSimpleMatch(JToken criteriaToken, int unitNum, string eventCodeType, string eventCode)
        {
            int? unitNumJSON = (int)criteriaToken["UnitNum"];
            string eventCodeTypeJSON = (string)criteriaToken["EventCodeType"];
            string eventCodeJSON = (string)criteriaToken["EventCode"];

            //Check basics
            bool reqFlag = unitNum == unitNumJSON && eventCodeType == eventCodeTypeJSON && eventCode == eventCodeJSON;
            //Console.WriteLine("Simple criteria. (in/json)  UnitNum: " + unitNum + "/" + unitNumJSON + " CodeType: " + eventCodeType + "/" + eventCodeTypeJSON + " Code: " + eventCode + "/" + eventCodeJSON);

            return reqFlag;
        }

        private bool CheckIfMatch(JToken criteriaToken, long eventEpoch, DataTable rawEventDataTable)
        {
            //Get criteria
            int? unitNumJSON = (int)criteriaToken["UnitNum"];
            string eventCodeTypeJSON = (string)criteriaToken["EventCodeType"];
            string eventCodeJSON = (string)criteriaToken["EventCode"];
            int numSeconds = (int?)criteriaToken["NumSeconds"] ?? 900;  //If nothing passed in, we'll set an upper limit of 15 minutes
            bool notFlag = (bool?)criteriaToken.SelectToken("NotFlag") ?? false;   //checks for the absense
            bool beforeFlag = (bool?)criteriaToken.SelectToken("BeforeFlag") ?? false;
            //Console.WriteLine("Basic criteria. (in/json)  UnitNum: " + unitNum + "/" + unitNumJSON + " CodeType: " + eventCodeType + "/" + eventCodeTypeJSON + " Code: " + eventCode + "/" + eventCodeJSON);
            //Console.WriteLine("Optional criteria. Seconds:" + numSeconds + " NotFlag: " + notFlag + " beforeFlag: " + beforeFlag);

            //Figure upper/lower bound for time
            long lowerBound, upperBound;
            if (beforeFlag)
            {
                upperBound = eventEpoch;
                lowerBound = eventEpoch - numSeconds;
            }
            else
            {
                lowerBound = eventEpoch;
                upperBound = eventEpoch + numSeconds;
            }

            //Search more in the results of the broader query above
            var query = from Event in rawEventDataTable.AsEnumerable()
                        where Event.Field<long>("DeviceDate") >= lowerBound &&
                              Event.Field<long>("DeviceDate") <= upperBound &&
                              Event.Field<int>("UnitNum") == unitNumJSON &&
                              Event.Field<string>("EventCodeType") == eventCodeTypeJSON &&
                              Event.Field<string>("EventCode") == eventCodeJSON
                        select Event;

            //Figure out actual return value now
            bool retVal = false;
            if (query.Count() > 0 && !notFlag)
                retVal = true;
            if (query.Count() == 0 && notFlag)
                retVal = true;

            return retVal;
        }

        private void AddNewResolvedEvent(string resolutionName, string resolution, string confidence, string path, long deviceEpoch)
        {
            using (SqlConnection connection = new SqlConnection(dbConnectionString))
            {
                connection.Open();

                string sql = "INSERT INTO ResolvedEvent(ResolutionName,Resolution,Confidence,JSONPath,ClosedFlag,DeviceEpoch) VALUES(@resolutionName,@resolution,@confidence,@path,@closedFlag,@deviceEpoch)";
                SqlCommand cmd = new SqlCommand(sql, connection);
                cmd.Parameters.Add("@resolutionName", SqlDbType.VarChar).Value = resolutionName;
                cmd.Parameters.Add("@resolution", SqlDbType.VarChar).Value = resolution;
                cmd.Parameters.Add("@confidence", SqlDbType.VarChar).Value = confidence;
                cmd.Parameters.Add("@path", SqlDbType.VarChar).Value = path;
                cmd.Parameters.Add("@closedFlag", SqlDbType.Bit).Value = 0;
                cmd.Parameters.Add("@deviceEpoch", SqlDbType.BigInt).Value = deviceEpoch;
                cmd.CommandType = CommandType.Text;
                cmd.ExecuteNonQuery();
            }
        }

        private void UpdateResolvedEvent(string resolutionName, string resolution, string confidence, string path, int id)
        {
            using (SqlConnection connection = new SqlConnection(dbConnectionString))
            {
                connection.Open();

                string sql = "update ResolvedEvent set ResolutionName=@resolutionName,Resolution=@resolution,Confidence=@confidence,JSONPath=@path where id=@id";
                SqlCommand cmd = new SqlCommand(sql, connection);
                cmd.Parameters.Add("@resolutionName", SqlDbType.VarChar).Value = resolutionName;
                cmd.Parameters.Add("@resolution", SqlDbType.VarChar).Value = resolution;
                cmd.Parameters.Add("@confidence", SqlDbType.VarChar).Value = confidence;
                cmd.Parameters.Add("@path", SqlDbType.VarChar).Value = path;
                //cmd.Parameters.Add("@closedFlag", SqlDbType.Bit).Value = closedFlag;
                cmd.Parameters.Add("@id", SqlDbType.Int).Value = id;
                cmd.CommandType = CommandType.Text;
                cmd.ExecuteNonQuery();
            }
        }

        private DataTable GetOpenEvents()
        {
            //15 minute window
            return SqlSelect(@"select * from ResolvedEvent where closedFlag = 0 and DbDateTime >= DATEADD (minute , -15 +  datepart(tz,getutcdate() AT TIME ZONE 'Pacific Standard Time'), getutcdate())");
        }

        private DataTable GetRawEvents()
        {
            //15 minute window
            return SqlSelect(@"select * from Event where DeviceDateTime >= DATEADD (minute , -15 +  datepart(tz,getutcdate() AT TIME ZONE 'Pacific Standard Time'), getutcdate()) order by DeviceDate");
        }

        private DataTable SqlSelect(string sql)
        {
            using (SqlConnection connection = new SqlConnection(dbConnectionString))
            {
                connection.Open();
                SqlCommand cmd = new SqlCommand(sql, connection);
                cmd.CommandType = CommandType.Text;
                SqlDataReader rd = cmd.ExecuteReader();
                DataTable dt = new DataTable();
                dt.Load(rd);
                rd.Close();

                return dt;
            }
        }

        private string GetJSON()
        {
            return @"{
              'EVENT_Garage_Opened': {
                'Resolution': 'Garage Door Opened',
                'Confidence': 'Certain',
                'Explanation': 'n/a',
                'Criteria': {
                    'UnitNum': 8,
                  'EventCodeType': 'O',
                  'EventCode': 'O'
                },
                'EVENT_Garage_Closed': {
                    'Resolution': 'Someone has arrived or left via the garage',
                  'Confidence': 'Medium',
                  'Explanation': 'If the door open/closed w/in 2 minutes, someone probably came or went',
                  'Criteria': {
                        'UnitNum': 8,
                    'EventCodeType': 'O',
                    'EventCode': 'C'
                  },
                  'EVENT_No_Mvmnt_After': {
                        'Resolution': 'Someone has left via the garage',
                    'Confidence': 'Medium',
                    'Explanation': 'Further, if NO movement within 3 minutes - they probably left',
                    'Criteria': {
                            'UnitNum': 3,
                      'NumSeconds': 180,
                      'NotFlag': 'true',
                      'EventCodeType': 'M',
                      'EventCode': 'D'
                    }
                    },
                  'EVENT_Mvmnt_After': {
                        'Resolution': 'Someone has come via the garage',
                    'Confidence': 'Low',
                    'Explanation': 'If folks are still home - perhaps they did not leave',
                    'Criteria': {
                            'UnitNum': 3,
                      'NumSeconds': 180,
                      'EventCodeType': 'M',
                      'EventCode': 'D'
                    },
                    'EVENT_And_No_Mvmnt_Before': {
                            'Resolution': 'Some has come via the garage',
                      'Confidence': 'High',
                      'Explanation': 'Our confidence goes up because now we see that nobody was home before',
                      'Criteria': {
                                'UnitNum': 3,
                        'NumSeconds': 180,
                        'NotFlag': 'true',
                        'BeforeFlag': 'true',
                        'EventCodeType': 'M',
                        'EventCode': 'D'
                      }
                        }
                    }
                }
            },
              'EVENT_Front_Opened': {
                'Resolution': 'Front Door Opened',
                'Confidence': 'Certain',
                'Explanation': 'n/a',
                'Criteria': {
                    'UnitNum': 6,
                  'EventCodeType': 'O',
                  'EventCode': 'O'
                }
            }
        }";
        }

    }
}
