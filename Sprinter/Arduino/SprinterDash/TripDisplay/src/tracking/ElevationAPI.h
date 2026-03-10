#ifndef ElevationAPI_h
#define ElevationAPI_h

/*
  ElevationAPI — Auto-calibrate barometric altitude using Open Topo Data
  ======================================================================
  The MPL3115A2 barometer drifts with weather patterns (pressure changes),
  so the reported altitude can be off by 50–200+ feet on a given day.

  This module queries the free Open Topo Data public API to look up the
  true ground elevation at the current GPS position, then computes an
  offset that is applied to every subsequent barometer reading:

      correctedElevation = rawBaroElevation + elevationOffset

  API details:
    Endpoint:  https://api.opentopodata.org/v1/aster30m?locations={lat},{lon}
    Dataset:   ASTER 30m (~30m horizontal resolution, near-global 83N-83S)
    Rate limit: 1 request/second, 1000 requests/day (free, no API key)
    Response:  {"results":[{"elevation":815.0,...}],"status":"OK"}

  Calibration strategy:
    - First calibration as soon as WiFi + GPS fix are both available
    - Re-calibrate every RECALIB_INTERVAL_MS (default 30 minutes)
    - Offset is persisted in PropBag (EEPROM) so it survives reboots
    - Uses a sanity check: ignores API results that differ from baro
      by more than MAX_REASONABLE_OFFSET_FT (default 2000 ft) to guard
      against bad GPS fixes or API errors

  Integration:
    Called from the main loop() after GPS fix is confirmed. The module
    manages its own timing and won't hit the API more than needed.
*/

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../logging/logger.h"

extern Logger logger;

// ---- Configuration ----

// Open Topo Data public API (free, no key required, 1000 calls/day)
#define ELEVATION_API_HOST    "api.opentopodata.org"
#define ELEVATION_API_DATASET "aster30m"    // near-global (83N-83S), 30m res.  Was ned10m (CONUS only)

// How often to re-calibrate (milliseconds)
// 30 minutes = 48 calls/day, well under the 1000/day free limit
#define RECALIB_INTERVAL_MS   1800000UL

// Safety limit: ignore API results more than this far from baro reading.
// Protects against bad GPS fixes (e.g. cold start near 0,0) or API errors.
#define MAX_REASONABLE_OFFSET_FT 2000

// HTTP request timeout (ms)
#define ELEVATION_API_TIMEOUT 8000


class ElevationAPI
{
  public:
    void init();

    // Call from loop() when WiFi is connected and GPS has fix.
    // Manages its own timing — safe to call every loop iteration.
    // rawBaroElevFeet = current barometer reading (feet) before offset
    // Returns true if a new calibration was just applied.
    bool update(float lat, float lon, int rawBaroElevFeet);

    // The computed offset (feet) to ADD to raw barometer readings.
    // Positive = baro reads too low, negative = baro reads too high.
    int  getElevationOffset();

    // Force an immediate recalibration on next update() call
    void forceRecalibrate();

    // Has at least one successful calibration occurred this session?
    bool isCalibrated();

    // Status for diagnostics / HTTP endpoint
    float getLastAPIElevation();     // last elevation returned by API (feet)
    unsigned long getLastCalibTime(); // millis() of last successful calibration
    int   getCalibrationCount();     // number of successful calibrations this session

  private:
    bool queryAPI(float lat, float lon, float &elevMeters);

    int   elevationOffset = 0;       // feet, add to raw baro reading
    bool  calibrated = false;        // true after first successful calibration
    float lastAPIElevFeet = 0;       // for diagnostics
    unsigned long lastCalibMillis = 0;
    unsigned long nextCalibMillis = 0;
    int   calibCount = 0;
};

#endif
