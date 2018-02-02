/*
 CircularBuffer.tpp - Circular buffer library for Arduino.
 Copyright (c) 2017 Roberto Lo Giacco.  All right reserved.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <string.h>

template<typename T> 
CircularBuffer<T>::CircularBuffer(__CB_ST__ s) {
	buffer = (T*) malloc(s * sizeof(T));
	sizeOfArray = s;
	head=buffer;
	tail=buffer;
	count=0;
}

template<typename T> 
CircularBuffer<T>::~CircularBuffer() {
}

template<typename T> 
bool CircularBuffer<T>::unshift(T value) {
	if (head == buffer) {
		head = buffer + sizeOfArray;
	}
	*--head = value;
	if (count == sizeOfArray) {
		if (tail-- == buffer) {
			tail = buffer + sizeOfArray - 1;
		}
		return false;
	} else {
		if (count++ == 0) {
			tail = head;
		}
		return true;
	}
}

template<typename T> 
bool CircularBuffer<T>::push(T value) {
	if (++tail == buffer + sizeOfArray) {
		tail = buffer;
	}
	*tail = value;
	if (count == sizeOfArray) {
		if (++head == buffer + sizeOfArray) {
			head = buffer;
		}
		return false;
	} else {
		if (count++ == 0) {
			head = tail;
		}
		return true;
	}
}

template<typename T> 
T CircularBuffer<T>::shift() {
	void(* crash) (void) = 0;
	if (count <= 0) crash();
	T result = *head++;
	if (head >= buffer + sizeOfArray) {
		head = buffer;
	}
	count--;
	return result;
}

template<typename T> 
T CircularBuffer<T>::pop() {
	void(* crash) (void) = 0;
	if (count <= 0) crash();
	T result = *tail--;
	if (tail < buffer) {
		tail = buffer + sizeOfArray - 1;
	}
	count--;
	return result;
}

template<typename T> 
T inline CircularBuffer<T>::first() {
	return *head;
}

template<typename T> 
T inline CircularBuffer<T>::last() {
	return *tail;
}

template<typename T> 
T CircularBuffer<T>::operator [](__CB_ST__ index) {
	return *(buffer + ((head - buffer + index) % sizeOfArray));
}

template<typename T> 
__CB_ST__ inline CircularBuffer<T>::size() {
	return count;
}

template<typename T> 
__CB_ST__ inline CircularBuffer<T>::available() {
	return sizeOfArray - count;
}

template<typename T> 
__CB_ST__ inline CircularBuffer<T>::capacity() {
	return sizeOfArray;
}

template<typename T> 
bool inline CircularBuffer<T>::isEmpty() {
	return count == 0;
}

template<typename T> 
bool inline CircularBuffer<T>::isFull() {
	return count == sizeOfArray;
}

template<typename T> 
void inline CircularBuffer<T>::clear() {
	memset(buffer, 0, sizeof(buffer));
	head = tail = buffer;
	count = 0;
}

#ifdef CIRCULAR_BUFFER_DEBUG
template<typename T> 
void inline CircularBuffer<T>::debug(Print* out) {
	for (__CB_ST__ i = 0; i < sizeOfArray; i++) {
		int hex = (int)buffer + i;
		out->print(hex, HEX);
		out->print("  ");
		out->print(*(buffer + i));
		if (head == buffer + i) {
			out->print(" head");
		} 
		if (tail == buffer + i) {
			out->print(" tail");
		}
		out->println();
	}
}

template<typename T> 
void inline CircularBuffer<T>::debugFn(Print* out, void (*printFunction)(Print*, T)) {
	for (__CB_ST__ i = 0; i < sizeOfArray; i++) {
		int hex = (int)buffer + i;
		out->print(hex, HEX);
		out->print("  ");
		printFunction(out, *(buffer + i));
		if (head == buffer + i) {
			out->print(" head");
		} 
		if (tail == buffer + i) {
			out->print(" tail");
		}
		out->println();
	}
}
#endif