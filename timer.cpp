/*
 * timer.cpp
 *
 *  Created on: 13-Jun-2014
 *      Author: sony
 */

//#include "timer.h"
#include <sys/time.h>

double timerval () {
struct timeval st;
gettimeofday (&st, NULL);
return st.tv_sec + st.tv_usec * 1e-6;
}



