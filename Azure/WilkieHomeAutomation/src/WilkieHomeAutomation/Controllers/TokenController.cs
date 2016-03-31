using System;
using System.Linq;
using Microsoft.AspNet.Mvc;
using System.IdentityModel.Tokens;
using System.Security.Claims;
using Microsoft.AspNet.Authorization;
using System.Security.Principal;
using Microsoft.AspNet.Authentication.JwtBearer;
using System.IdentityModel.Tokens.Jwt;
using Microsoft.Extensions.Logging;


namespace WilkieHomeAutomation.Controllers
    {
        [Route("api/[controller]")]
        public class TokenController : Controller
        {
            private readonly TokenAuthOptions tokenOptions;
            private readonly ILogger<TokenController> logger;

            public TokenController(TokenAuthOptions tokenOptions, ILogger<TokenController> logger)
            {
                this.tokenOptions = tokenOptions;
                this.logger = logger;
            }

            /// <summary>
            /// Check if currently authenticated. Will throw an exception of some sort which shoudl be caught by a general
            /// exception handler and returned to the user as a 401, if not authenticated. Will return a fresh token if
            /// the user is authenticated, which will reset the expiry.
            /// </summary>
            /// <returns></returns>
            [HttpGet]
            [Authorize("Bearer")]
            public dynamic Get()
            {
                /* 
                THIS METHOD ALLOWS A USER WITH A VALID TOKEN TO REMAIN LOGGED IN FOREVER, WITH NO WAY OF EVER EXPIRING THEIR
                RIGHT TO USE THE APPLICATION - SHORT OF STARTING THE SERVER.
                */
                bool authenticated = false;
                string user = null;
                int entityId = -1;
                string token = null;
                DateTime? tokenExpires = default(DateTime?);

                var currentUser = HttpContext.User;
                if (currentUser != null)
                {
                    authenticated = currentUser.Identity.IsAuthenticated;
                    if (authenticated)
                    {
                        user = currentUser.Identity.Name;

                        foreach (Claim c in currentUser.Claims) if (c.Type == "EntityID") entityId = Convert.ToInt32(c.Value);
                        tokenExpires = DateTime.UtcNow.AddMonths(3);
                        token = GetToken(currentUser.Identity.Name, tokenExpires);
                    }
                }

                 return new { authenticated = authenticated, user = user, entityId = entityId, token = token, tokenExpires = tokenExpires };
            }

            public class AuthRequest
            {
                public string pat { get; set; }
            }

            /// <summary>
            /// Request a new token for a given username/password pair.
            /// </summary>
            /// <param name="req"></param>
            /// <returns></returns>
            [HttpPost]
            public dynamic Post([FromBody] AuthRequest req)
            {
                // Yup, this should be kept on an offsite secrets store - but I'm lazy and my taget is not juicy
                if(req.pat == 
                {
                    DateTime? expires = DateTime.UtcNow.AddMonths(1);
                    var token = GetToken("RFHubUser", expires);

                logger.LogInformation("-TEST");
                System.Diagnostics.Trace.TraceWarning("TEST");
                System.Diagnostics.Trace.Flush();

                return new { authenticated = true, entityId = 1, token = token, tokenExpires = expires };
                }
                return new { authenticated = false };
            }

            private string GetToken(string user, DateTime? expires)
            {
                var handler = new JwtSecurityTokenHandler();

                // Creating a generic identity used for the RFHub running on the PI
                ClaimsIdentity identity = new ClaimsIdentity(new GenericIdentity(user, "RFHub"), new[] { new Claim("EntityID", "1", ClaimValueTypes.Integer) });

                var securityToken = handler.CreateToken(
                    issuer: tokenOptions.Issuer,
                    audience: tokenOptions.Audience,
                    signingCredentials: tokenOptions.SigningCredentials,
                    subject: identity,
                    expires: expires
                    );
                return handler.WriteToken(securityToken);
            }
        }
    }
