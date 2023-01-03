#include "TestData.h"

void TestData::NextRow()
{
    rowPtr=rowPtr+9;
    if(rowPtr>=sizeof(simData))
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