#ifndef GPSSimulator_h
#define GPSSimulator_h

#include "../logging/logger.h"

//
// GPSSimulator.h — Simulated GPS route: Home → Everett → Home via I-5
//
// Provides fake GPS positions for testing Traccar uploads without real hardware.
// Plays 30 waypoints at 10-second intervals (5 minutes total).
// Starts at home, drives north on I-5 to Everett, then returns.
// Final waypoint is back inside HOME_RADIUS_M to trigger geofence.
//

struct SimWaypoint {
    float lat;
    float lon;
    float elevFeet;   // simulated barometric altitude
    float speedMph;   // simulated vehicle speed
};

// 30 waypoints: 15 northbound (home → Everett), 15 southbound (Everett → home)
// Roughly follows I-5 corridor.  Speeds ramp up/down at endpoints.
static const SimWaypoint simRoute[] = {
    // --- Northbound: Home → Everett ---
    // 0: Start at home, engine just started
    { 47.7566, -122.2744,  450.0,   0.0 },
    // 1: Pulling out of neighborhood
    { 47.7590, -122.2740,  440.0,  25.0 },
    // 2: Merging onto local road
    { 47.7650, -122.2730,  420.0,  35.0 },
    // 3: Approaching I-5 on-ramp
    { 47.7730, -122.2710,  400.0,  40.0 },
    // 4: On I-5 accelerating
    { 47.7850, -122.2680,  380.0,  55.0 },
    // 5: Cruising I-5 north past Kenmore
    { 47.8020, -122.2590,  350.0,  62.0 },
    // 6: I-5 north past Bothell
    { 47.8200, -122.2480,  320.0,  65.0 },
    // 7: I-5 past Canyon Park
    { 47.8400, -122.2400,  300.0,  63.0 },
    // 8: I-5 past Alderwood Mall
    { 47.8600, -122.2420,  280.0,  60.0 },
    // 9: I-5 Lynnwood
    { 47.8800, -122.2600,  260.0,  64.0 },
    // 10: I-5 Mountlake Terrace
    { 47.9000, -122.2700,  240.0,  62.0 },
    // 11: I-5 approaching Everett
    { 47.9200, -122.2600,  220.0,  58.0 },
    // 12: I-5 south Everett
    { 47.9450, -122.2400,  200.0,  50.0 },
    // 13: Exiting I-5 in Everett
    { 47.9650, -122.2200,  180.0,  35.0 },
    // 14: Stopped in Everett
    { 47.9790, -122.2020,  170.0,   0.0 },

    // --- Southbound: Everett → Home ---
    // 15: Leaving Everett
    { 47.9700, -122.2100,  175.0,  25.0 },
    // 16: Back on I-5 south
    { 47.9500, -122.2350,  200.0,  50.0 },
    // 17: I-5 south Everett
    { 47.9300, -122.2550,  220.0,  60.0 },
    // 18: I-5 Mountlake Terrace
    { 47.9050, -122.2680,  240.0,  63.0 },
    // 19: I-5 Lynnwood
    { 47.8850, -122.2580,  260.0,  65.0 },
    // 20: I-5 past Alderwood
    { 47.8650, -122.2410,  280.0,  62.0 },
    // 21: I-5 Canyon Park
    { 47.8430, -122.2390,  300.0,  64.0 },
    // 22: I-5 Bothell
    { 47.8220, -122.2470,  320.0,  63.0 },
    // 23: I-5 south past Kenmore
    { 47.8050, -122.2570,  340.0,  60.0 },
    // 24: I-5 decelerating
    { 47.7880, -122.2660,  360.0,  55.0 },
    // 25: Exiting I-5
    { 47.7760, -122.2700,  390.0,  40.0 },
    // 26: Local road
    { 47.7680, -122.2720,  410.0,  35.0 },
    // 27: Approaching neighborhood
    { 47.7620, -122.2735,  430.0,  25.0 },
    // 28: Almost home
    { 47.7580, -122.2742,  445.0,  15.0 },
    // 29: Home — inside geofence radius
    { 47.7566, -122.2744,  450.0,   0.0 },
};

static const int SIM_ROUTE_LENGTH = sizeof(simRoute) / sizeof(simRoute[0]);

class GPSSimulator
{
public:
    void init()
    {
        waypointIndex = 0;
        nextAdvance = 0;
        finished = false;
        logger.log(INFO, "GPS Simulator initialized — %d waypoints, route: Home -> Everett -> Home", SIM_ROUTE_LENGTH);
    }

    // Call every loop.  Advances waypoint every intervalMs.
    void update(unsigned long intervalMs = 10000)
    {
        if (finished) return;

        if (millis() >= nextAdvance)
        {
            if (waypointIndex < SIM_ROUTE_LENGTH - 1)
            {
                waypointIndex++;
                logger.log(VERBOSE, "GPS Sim waypoint %d/%d: %f,%f  %fmph", 
                    waypointIndex, SIM_ROUTE_LENGTH, 
                    simRoute[waypointIndex].lat, simRoute[waypointIndex].lon,
                    simRoute[waypointIndex].speedMph);
            }
            else
            {
                if (!finished)
                    logger.log(INFO, "GPS Simulator route complete — arrived home");
                finished = true;  // stay on last point (home)
            }
            nextAdvance = millis() + intervalMs;
        }
    }

    bool hasFix()       { return true; }  // always has "fix"
    bool isFinished()   { return finished; }
    int  getIndex()     { return waypointIndex; }

    float getLatitude()   { return simRoute[waypointIndex].lat; }
    float getLongitude()  { return simRoute[waypointIndex].lon; }
    float getElevation()  { return simRoute[waypointIndex].elevFeet; }
    float getSpeed()      { return simRoute[waypointIndex].speedMph; }

private:
    int waypointIndex = 0;
    unsigned long nextAdvance = 0;
    bool finished = false;
};

#endif
