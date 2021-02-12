#############################################################################
#
# Makefile for librf24mesh examples on Raspberry Pi
#
# By:  TMRh20
# Date:    2013/09
#
# Description:
# ------------
# use make all and make install to install the examples
# You can change the install directory by editing the prefix line
#
prefix := /usr/local

# The recommended compiler flags for the Raspberry Pi
CCFLAGS=-march=armv6zk -mtune=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard -Ofast -Wall -pthread

HubMaster: HomeAutomationWebAPI.o WebAPI.o EpochAccumulator.o HubMaster.cpp
	g++ ${CCFLAGS} -I/usr/local/include/RF24/.. -I.. -lrf24 -lcurl HomeAutomationWebAPI.o WebAPI.o EpochAccumulator.o HubMaster.cpp -o HubMaster

HomeAutomationWebApi: HomeAutomationWebApi.cpp WebAPI.o
	g++ HomeAutomationWebAPI.cpp -c 

WebAPI: WebAPI.cpp
	g++ WebAPI.cpp -c 

EpochAccumulator: EpochAccumulator.cpp 
	g++ EpochAccumulator.cpp -c 
