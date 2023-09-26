#include "BTDevice.hpp"

boolean BTDevice::getIsNewDataAvailable() 
{
	boolean isNewDataAvailable = newDataAvailable;
	newDataAvailable = false;
	return (isNewDataAvailable);
}