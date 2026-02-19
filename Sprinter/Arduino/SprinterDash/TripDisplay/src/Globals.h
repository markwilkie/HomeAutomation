#ifndef GLOBALS_H
#define GLOBALS_H

// ---- Feature flags ----
#define DISABLE_GPS      // Comment out or remove this line to re-enable real GPS hardware
#define SIMULATE_GPS     // When defined (with DISABLE_GPS), uses fake route data and still uploads to Traccar

// Convenience: true when Traccar/tracking code should be compiled (real OR simulated GPS)
#if !defined(DISABLE_GPS) || defined(SIMULATE_GPS)
  #define GPS_ENABLED
#endif

//
// Globals.h — extern declarations for shared objects
//
// When .ino files lived in the sketch root, the Arduino IDE concatenated them
// into one translation unit so globals were visible everywhere.  Now that
// the source lives in src/ as separate .cpp files, each file is its own
// translation unit and needs explicit extern declarations.
//
// The actual definitions live in:
//   logger  → src/net/VanWifi.cpp  (Logger logger;)
//   server  → src/net/VanWifi.cpp  (WebServer server;)
//

#include "logging/logger.h"

// The one-and-only Logger instance (defined in VanWifi.cpp)
extern Logger logger;

#endif
