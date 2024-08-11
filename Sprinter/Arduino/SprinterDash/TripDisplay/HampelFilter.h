/*** Arduino Hampel Filter Library ***

This library implements a Hampel filter for numerical data on Arduino. A Hampel filter is useful to find outliers in data. The sensitivity of this Hampel filter and the size of the data window that the filter looks at are adjustable.

The filter works on any numerical values, like 22, 35.98 and -232.43, as long as they are between -2550 and 2550. The filter delivers two-digit precision.

Further information about the Hampel filter can be found here:
    (1) https://www.seedtest.org/upload/cms/user/presentation2Remund2.pdf
        (checked on 2016-02-11)
    (2) http://dimacs.rutgers.edu/Workshops/DataCleaning/slides/pearson2.pdf
        (checked on 2016-02-11)

Library written by Florian Roscheck
Simplified BSD license, all text above must be included in any redistribution
***/

#ifndef HampelFilter_h
#define HampelFilter_h

#include <inttypes.h>
#include <stddef.h>

class HampelFilter {
public:
  // constructor
  void init(uint8_t i_window_size, int16_t i_scaling_factor, int8_t i_override_num);

  // adjustable filter parameters
  uint8_t window_size;  // no. of data points in the filter buffer
  uint16_t scaling_factor;  // scaling factor setting the outlier detection threshold
  uint8_t override_num;  // no. of times in a row outlier is received before added to buffer

  void write(int16_t i_new_value_flt);  // add value to buffer
  bool writeIfNotOutlier(int16_t i_new_value_flt);  // returns true if not an outlier - and not more than override_num in a row
  int16_t readMedian();         // read median
  int16_t readMAD();          // read median absolute deviation (MAD)
  int16_t readOrderedValue(int i_position_in_list); // read specific value from buffer
  bool checkIfOutlier(int16_t i_qry_value); // check if queried value is an outlier

private:
  // outlier override state
  int8_t outlierCount;  // number of times IN A ROW an outlier was found
  int32_t outlierSum;   // used to calc avg if overriding
  bool buffer_filled;   // true when the initial buffer is full of values for the first time

  // heap allocation of buffers
  int16_t* inbuffer;    // cyclic buffer for incoming values
  int16_t* sortbuffer;  // sorted buffer to determine median
  int16_t* difmedbuffer;  // sorted buffer of absolute differences to median

  uint8_t buffer_ptr;   // pointer to current value index of cyclic inbuffer
  uint8_t median_ptr;   // pointer to median value of sortbuffer and difmedbuffer

  int16_t readMedianInt();// read median as integer (123 instead of 1.23)
  int16_t readMADInt(); // read MAD as integer
  void combSort11(int16_t *input, size_t size); // perform comb sort on data
};

#endif
