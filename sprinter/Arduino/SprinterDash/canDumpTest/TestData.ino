#include "TestData.h"

void TestData::NextRow()
{
    const int arrLen = sizeof(simData) / sizeof(simData[0]);

    rowPtr=rowPtr+9;
    if(rowPtr>=arrLen)
        rowPtr=0;
}

unsigned long TestData::GetId()
{
    return simData[rowPtr];
}

unsigned int TestData::GetData(int idx)
{
    return simData[rowPtr+1+idx];
}

unsigned int * TestData::GetDataPtr()
{
    return (unsigned int*)simData+rowPtr+1;
}