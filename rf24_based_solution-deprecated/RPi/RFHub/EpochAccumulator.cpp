#include "EpochAccumulator.h"


float EpochAccumulator::Sum()
{
   float sum=accumulate(valueList.begin(),valueList.end(),0.0);
   //cout << "Sum: " << sum << '\n';
   return sum;
}

void EpochAccumulator::DumpEpochValues()
{
   list<long>::iterator epochIterator;
   list<float>::iterator valueIterator;

   valueIterator=valueList.begin();
   epochIterator=epochList.begin();
   while(epochIterator != epochList.end()) 
   {
      //cout << "Epoch: " << *epochIterator << " Count: " << *valueIterator << '\n';

      epochIterator++;
      valueIterator++; //keep in sync
   }
}

void EpochAccumulator::AddEpochValue(long epoch,float value)
{
   if(value<=0)
      return;
   
   epochList.push_back(epoch);
   valueList.push_back(value);
}

void EpochAccumulator::RemoveOldEpochs(long epochThreshold)
{
   list<long>::iterator epochIterator;
   list<float>::iterator valueIterator;

   valueIterator=valueList.begin();
   epochIterator=epochList.begin();
   while(epochIterator != epochList.end()) 
   {
      if(*epochIterator <= epochThreshold)
      {
         //cout << "Removing Epoch: " << *epochIterator << " Count: " << *valueIterator << '\n';
         epochList.erase(epochIterator++);
         valueList.erase(valueIterator++);
      }
      else
      {
         epochIterator++;
         valueIterator++; //keep in sync
      }
   }
}