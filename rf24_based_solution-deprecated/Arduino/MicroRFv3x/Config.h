//
// function pointers for extensibility
//
// see the fan extension as an example
//
typedef void (*SetupWorkFunctionPointer) (); //Type for function pointer which is called once at setup
typedef void (*DoWorkFunctionPointer) (); //Type for function pointer which is called once every sleep loop
SetupWorkFunctionPointer setupWorkFunction;
DoWorkFunctionPointer doWorkFunction;

//
// Misc
//
#define CONTEXT_SEND_FREQ 10             // How often to send context  (every 'n' state sends)
