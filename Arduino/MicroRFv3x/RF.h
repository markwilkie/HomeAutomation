//
// RF Setup
//
#define CONTEXT_PIPE 0xF0F0F0F0F2LL     // 5 on PI - Pipe to transmit context on
#define CONTEXT_PAYLOADSIZE 3          // Size of package we're sending over the wire
#define STATE_PIPE   0xF0F0F0F0F1LL     // 4 on PI - Pipe to transmit state on
#define STATE_PAYLOADSIZE 16            // Size of package we're sending over the wire
#define EVENT_PIPE   0xF0F0F0F0E2LL     // 2 on PI - Pipe to transmit event on
#define EVENT_PAYLOADSIZE 8             // Size of package we're sending over the wire
#define ALARM_PIPE   0xF0F0F0F0E3LL     // 3 on PI - Pipe to transmit alarms on  (uses event payload size and builders)