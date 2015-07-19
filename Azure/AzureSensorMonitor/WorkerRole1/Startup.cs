using Owin;
using System.Web.Http;

namespace WorkerRole1
{
    class Startup
    {
        public void Configuration(IAppBuilder app)
        {
            HttpConfiguration config = new HttpConfiguration();

            //Enable cross domain
            config.EnableCors();
            
            //Routing
            config.Routes.MapHttpRoute("SensorRoute", "Sensor/{action}/{id}/{rows}", new { controller = "Sensor", id = RouteParameter.Optional, rows = RouteParameter.Optional });
            config.Routes.MapHttpRoute("EventRoute", "Event/{action}/{eventType}/{id}", new { controller = "Event", id = RouteParameter.Optional, eventType = RouteParameter.Optional });
            config.Routes.MapHttpRoute("AlarmRoute", "Alarm/{action}/{alarmType}/{alarmCode}", new { controller = "Alarm", alarmCode = RouteParameter.Optional, alarmType = RouteParameter.Optional });
            app.UseWebApi(config);
        }
    }
}