#ifndef filter_h
#define filter_h

#include <inttypes.h>
#include <stddef.h>

class Filter {
public:
  void init(int i_window_size, int i_scaling_factor, int i_override_num);

  // adjustable filter parameters
  int window_size;  // no. of data points in the filter buffer
  int scaling_factor;  // scaling factor setting the outlier detection threshold
  int override_num;  // no. of times in a row outlier is received before added to buffer

  void write(int i_new_value);  // add value to buffer
  bool writeIfNotOutlier(int i_new_value);  // returns true if not an outlier - and not more than override_num in a row
  bool checkIfOutlier(int i_qry_value); // check if queried value is an outlier
  int readAvg();

private:
  //running avg
  long currentSum;   // used to calc avg

  // outlier override state
  int outlierCount;  // number of times IN A ROW an outlier was found
  long outlierSum;   // used to calc avg if overriding
  bool buffer_filled;   // true when the initial buffer is full of values for the first time

  // heap allocation of buffers
  int* inbuffer;    // cyclic buffer for incoming values
  int buffer_ptr;   // pointer to current value index of cyclic inbuffer
};

#endif