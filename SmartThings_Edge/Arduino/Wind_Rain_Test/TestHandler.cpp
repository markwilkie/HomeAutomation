#include "TestHandler.h"

int TestHandler::getVal()
{
  return windRain.getTestVal();
}

void TestHandler::setVal(int val)
{
  windRain.setTestVal(val);
}
