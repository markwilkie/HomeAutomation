
## Overview
Using Sumsung Smartthings hub to play around with home automation stuff.  Smartthings (ST) is moving away from groovy/js (classic) to new style web hook+api style devices.  Rumor has it that they even sold the hub....  This means that as of right now (Feb 2021) things are a little in limbo.  

Currently I've got something working (although it's clunky) for measuring ORP and pH in my spa using an ESP32 devkitv1 board and Atlas Scientific consumer ORP and pH probes.

Next, I will explore using MQTT on Azure as a pattern so as not to take a new dependency on the hub connected devices that ST seems to be discontinuing.  (maybe)

## Tools
- Groovy IDE: https://graph-na04-useast2.api.smartthings.com/
- Smartthings CLI: https://github.com/SmartThingsCommunity/smartthings-cli

## Dependencies
- Arduino --> Smartthings: https://github.com/DanielOgorchock/ST_Anything
- Atlas Scientific consumer probes with analog isolator
    -  Note: Strip CR from sample code for calibration, and be sure and set pinmode to INPUT on samples

## SmartThings Notes/References:
- DTH Migration Tutorial: https://community.smartthings.com/t/custom-capability-and-cli-developer-preview 
- Custom Capabilities Docs: https://smartthings.developer.samsung.com/docs/Capabilities/custom-capabilities.html
- API docs:  https://smartthings.developer.samsung.com/docs/api-ref/st-api.html
- Community Browser: http://thingsthataresmart.wiki/index.php?title=How_to_Quick_Browse_the_Community-Created_SmartApps_Forum_Section
- VID Selector: https://glitch.com/~vid-selector
- ST-Anything capabilities: https://github.com/DanielOgorchock/ST_Anything/tree/master/devicetypes/ogiewon/st-capabilities 