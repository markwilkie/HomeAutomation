# HomeAutomation
Arduino, Raspberry Pi, and Windows Azure code for RF based home automation


Arduino sketches use the library from http://tmrh20.github.io/RF24/ 

RPi config file reader from http://www.hyperrealm.com/libconfig/

Github is here, but it didn't compile for me:  https://github.com/hyperrealm/libconfig

To compile config file reader:
- Copy, then unzip the libary onto your Pi
- CD into the directory and type 'sh ./configure --disable-cxx'
- make
- sudo make install
