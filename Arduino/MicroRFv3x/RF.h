//
// RF Setup
//
#define MAX_PAYLOADSIZE 30      // Max size of package we're sending over the wire

#define CONTEXT_PIPE 0xF0F0F0F0F2LL     // 5 on PI - Pipe to transmit context on
#define STATE_PIPE   0xF0F0F0F0F1LL     // 4 on PI - Pipe to transmit state on
#define EVENT_PIPE   0xF0F0F0F0E2LL     // 2 on PI - Pipe to transmit event on
#define ALARM_PIPE   0xF0F0F0F0E3LL     // 3 on PI - Pipe to transmit alarms on  (uses event payload size and builders)
