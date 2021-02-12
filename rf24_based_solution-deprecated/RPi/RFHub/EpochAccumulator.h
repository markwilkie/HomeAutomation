#include <iostream>
#include <list>
#include <numeric>      // std::accumulate
using namespace std;


class EpochAccumulator
{
public:

    void DumpEpochValues();
    void AddEpochValue(long epoch,float value);
    float Sum();
    void RemoveOldEpochs(long epochThreshold);

private:
    list<long> epochList;
    list<float> valueList;
};