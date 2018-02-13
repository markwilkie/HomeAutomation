### Overview
This is a purpose built battery/solar monitoring system.  It's goal is to track all Amps in and out of the system, including solar, alternature, living area, and inverter.

### Screen decoder
(Yes, the fact that a decoder is needed is not good.  Apparently, I'm not a UI designer)

Before we get to the screens, a couple of things:
-  The "circuits" are: 0) Aux - low voltage panel for the living area, 1) Slr - solar panel, 2) Inv - Inverter, and 3) Str - Van starter battery and alternator
-  The "time buckets" are: cur - real time value, min - last minute, hr - last hour, day - last day, and mth - last 30 days

Main Screen
-  SoC> State of Charge.  At its core, this is a straight calculation based on voltage for my specific batteries.  However, the batteries need to be "at rest" for quite some time before this is an accurate indication.  Given this, the SoC is affected by tracking the in/out of the amps.  It is then reset using voltage when the battery is resting - presumably most every night.
-  V> Battery voltage.  Remember, this will go way up during charging, and down a bit under heavy load.  Using SoC for a better indicator.
-  Ah> This is the (close to real-time) net amps (as measured in Amp hours) that is going through the system.  For example, if the solar was putting out 4Ah, and the fridge was pulling 3Ah, it would read 1Ah.
-  Hr> This is a "guestament" of how many hours are remaining.  Note that is the "real time" guess, and it's using the current amp load only.  This does not take into account trends.  Currently, I don't have a screen for that, but can add one if warranted.

Sum Screen
-  __circuit__>__bucket__> Shows the sum Ah value for a specific "circuit" and "time bucket".  See above for the circuits and buckets.
-  (+) Sum of charge for **all* circuits according to the specific time bucket

Average Screen
-  Same as 'sum', except for it shows averages

Status Screen
-  Rst> time since SoC was set
-  T> temperature in F
-  Hz> cycles per second that the ADC's are being read  (should be 4 to 5)
-  Up> time since the board was reset (uptime)

