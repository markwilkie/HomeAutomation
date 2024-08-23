
#include "Filter.h"
#include <stdlib.h> // Include this to get the abs()
#include <Arduino.h>

void Filter::init(int i_window_size, int i_scaling_factor, int i_override_num){
        // no. of data points in the filter buffer
        window_size = i_window_size;

        // no. of times in a row outlier is received before added to buffer
        override_num = i_override_num;

        // set initial values
        outlierCount=0;
        outlierSum=0;
        buffer_filled=false;

        // scaling factor determining the outlier detection threshold
        scaling_factor = i_scaling_factor;

        currentSum=0;
        buffer_ptr = i_window_size;
        inbuffer = new int[i_window_size];

        //seed values
        for(int i=0;i<window_size;i++)
          inbuffer[i]=0;
}

// add value to buffer if not an outlier - and not more than override_num in a row
bool Filter::writeIfNotOutlier(int i_new_value) {

        // if buffer has not yet been filled once with non default values, add no matter what
        if(!buffer_filled) {
                //Serial.print("Adding value as buffer is not yet full: ");  Serial.println(i_new_value);
                write(i_new_value);
                return true;
        }

        // check if outlier and add if not
        if(!checkIfOutlier(i_new_value))
        {
                //Serial.print("Adding value: ");  Serial.println(i_new_value);
                outlierCount=0;
                outlierSum=0;
                write(i_new_value);
                return true;
        }
        else
        {
          //Serial.print("Outlier: ");  Serial.println(i_new_value);
        }

        // since it's an outlier, update outlier state
        outlierCount++;
        outlierSum+=i_new_value;
        
        // now check if we're over the override threshold
        if(outlierCount>=override_num)
        {
                //Serial.print("Adding value in override: ");  Serial.println(outlierSum/override_num);

                // write avg of the last n outliers
                write(outlierSum/override_num);
                outlierCount=0;
                outlierSum=0;     
                return true;           
        }

        return false;
}

// add value to filter buffers
void Filter::write(int i_new_value) {

        // set pointer for writing to cyclic buffer
        if(buffer_ptr==0) {
                //Serial.println("filled buffer once");
                buffer_filled = true;  //we've filled the buffer at least once w/ non default values
                buffer_ptr = window_size;
        }
        buffer_ptr--;

        //Add new value
        int old_value = inbuffer[buffer_ptr];
        if(i_new_value==old_value)
                return;
        inbuffer[buffer_ptr] = i_new_value;

        //calc new sum
        currentSum=currentSum-old_value;
        currentSum=currentSum+i_new_value;
}

int Filter::readAvg() {
        return currentSum/window_size;
}

bool Filter::checkIfOutlier(int i_qry_value) {
        int avgdelta=abs(i_qry_value-readAvg());
        int lastdelta=abs(i_qry_value-inbuffer[buffer_ptr]);
        return (avgdelta>scaling_factor)&&(lastdelta>scaling_factor);
}