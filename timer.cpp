/*This file contains the declaration of the function timerval() that is used in the benchmarks to
 *register time
 */
#include <sys/time.h>

double timerval () {
struct timeval st;
gettimeofday (&st, NULL);
return st.tv_sec + st.tv_usec * 1e-6;
}



