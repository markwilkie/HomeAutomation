//
// ESParklines – I <3 Sparklines!
// Sparklines for ESP8266/ESP32/Arduino Displays
//
// Copyright (c) 2020 karl@pitrich.com
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#pragma once

#include <string.h>
#include <functional>
#include <algorithm>
#include <FixedPoints.h>
#include <FixedPointsCommon.h>
#include "../logging/logger.h"

extern Logger logger;

template <typename T, typename std::enable_if<std::is_arithmetic<T>::value>::type* = nullptr>
class SparkLine
{
  using drawLineFunction = std::function<void(int x0, int y0, int x1, int y1)>;
  using Point = struct { int x; int y; };  
  using num_t = SFixed<15, 16>;

  float* container;
  const int capacity;
  int elements;
  drawLineFunction drawLine;
  float offset=0;

public:
  T findAbsMin() const {
    T lo = container[0];
    for (int i = 0; i < elements; i++) {
      if (container[i] < lo) lo = container[i];
    }
    return lo;
  }

  T findAbsMax() const {
    T hi = 0;
    for (int i = 0; i < elements; i++) {
      if (container[i] > hi) hi = container[i];
    }
    return hi;
  }

  float findAvg() const {
    float sum = 0;
	  if (elements > 0) {    
      for (int i = 0; i < elements; i++) {
        sum += container[i]-offset;
      } 
      sum /= elements;
    }
    return sum;
  }

  float findSum() const {
    float sum = 0;
	  if (elements > 0) {    
      for (int i = 0; i < elements; i++) {
        sum += container[i]-offset;
      }
    }
    return sum;
  }

  SparkLine(int _size, drawLineFunction _dlf)
    : container(new T[_size]),
      capacity(_size),
      drawLine(_dlf)
  {
    reset();
  }

  virtual ~SparkLine()
  {
    delete[] container;
  }

  void reset() 
  {
    memset(container, 0, capacity * sizeof(T));
    elements = 0;
    offset=0;
  }

  int getElements()
  {
    return elements;
  }

  void add(float _value) 
  {
    int baseline=1;
    float value=_value+offset;

    //reset whole thing to new offset to make sure all numbers are positive    
    if(value<baseline)
    {
      float diff=baseline-value;
      if (elements > 0) {    
        for (int i = 0; i < elements; i++) {
          float temp=container[i];
          container[i]+=diff;
        }
      }    
      offset+=diff;
      value=_value+offset;       
    }

    if (elements < capacity) {
      container[elements] = value;
      elements++;
      return;
    }
    
    memmove(container, &container[1], (capacity - 1) * sizeof(float));
    container[capacity - 1] = value;
  }

  
  /**
   * Draw Sparkline using passed drawLine function
   * 
   * @param x           left-most poont where to start the sparkline
   * @param y           bottom point of the sparkline – to match font redering
   * @param maxWidth    max width of the sparkline 
   * @param maxHeight   max height of the sparkline 
   * @param lineWidth   width of the stroke for the line to be drawn
   */
  void draw(int x, int y, 
            int maxWidth, int maxHeight, 
            int lineWidth = 1) const
  {
    if (elements < 2) {
      return;
    }

    num_t slope = 1.0f * (maxHeight - lineWidth);

    T lo = this->findAbsMin();
    T hi = this->findAbsMax();
    if (lo != hi) {
      slope /= hi - lo;
    }
    
    Point lastPoint = { 0, 0 };
    num_t segment = 0.0f;
    
    int maxSegments = elements;
    if (maxSegments > maxWidth) {
      maxSegments = maxWidth;
    }

    num_t pixelPerSegment = 1.0f;
    if (elements <= maxWidth) {
      pixelPerSegment = 1.0f * maxWidth / (elements - 1);
    }

    for (int i = 0; i < maxSegments; i++) {
      T value = container[i];
      num_t scaledValue = maxHeight - ((value - lo) * slope);
      scaledValue -= lineWidth / 2.0f;
      scaledValue += y;

      Point pt {
        .x = static_cast<int>(x + segment.getInteger()),
        .y = static_cast<int>(scaledValue.getInteger())
      };

      if (segment > 0) {
        drawLine(lastPoint.x, lastPoint.y, pt.x, pt.y);
      }

      lastPoint = pt;
      segment += pixelPerSegment;
    }
  }
};