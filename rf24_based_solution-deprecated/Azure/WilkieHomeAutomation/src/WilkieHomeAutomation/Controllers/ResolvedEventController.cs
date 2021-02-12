using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.AspNet.Mvc;
using System.IdentityModel.Tokens;
using System.Security.Claims;
using Microsoft.AspNet.Authorization;
using System.Security.Principal;
using Microsoft.AspNet.Authentication.JwtBearer;
using System.IdentityModel.Tokens.Jwt;
using Microsoft.WindowsAzure.Storage;
using Microsoft.WindowsAzure.Storage.Queue;
using Microsoft.Extensions.Configuration;
using WilkieHomeAutomation.Models;
using Newtonsoft.Json;


namespace WilkieHomeAutomation.Controllers
{
    [Route("api/[controller]")]
    public class ResolvedEventController : Controller
    {
        private ResolvedEventContext _context;
        private readonly IConfiguration config;

        public ResolvedEventController(ResolvedEventContext context, IConfiguration config)
        {
            _context = context;
            this.config = config;
        }

        // GET: api/resolvedevents
        [HttpGet]
        //[Authorize("Bearer")]
        public IEnumerable<object> Get()
        {
            var query = (
                from ResolvedEvent in _context.ResolvedEvents
                select new { ResolvedEvent = ResolvedEvent })
                    .OrderByDescending(e => e.ResolvedEvent.DeviceEpoch)
                    .Take(10)
                    .AsEnumerable();

            return query;
        }
    }
}
