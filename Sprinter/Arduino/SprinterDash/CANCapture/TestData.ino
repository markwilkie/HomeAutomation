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

void TestData::FillCanFrame(unsigned char *canFrame)
{
    for(int i=0;i<7;i++)
        canFrame[i]=(char)simData[rowPtr+2+i];
}

unsigned int TestData::GetData(int idx)
{
    return simData[rowPtr+1+idx];
}

unsigned int * TestData::GetDataPtr()
{
    return (unsigned int*)simData+rowPtr+1;
}
