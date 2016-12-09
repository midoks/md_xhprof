/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2016 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: midoks(midoks@163)                                           |
  +----------------------------------------------------------------------+
*/

#include "xhprof.h"

/**
 * ***********************
 * High precision timer related functions.
 * ***********************
 */

/**
 * Get time stamp counter (TSC) value via 'rdtsc' instruction.
 *
 * @return 64 bit unsigned integer
 * @author cjiang
 */
uint64 cycle_timer() {
  uint32 __a,__d;
  uint64 val;
  asm volatile("rdtsc" : "=a" (__a), "=d" (__d));
  (val) = ((uint64)__a) | (((uint64)__d)<<32);
  return val;
}



/**
 * Truncates the given timeval to the nearest slot begin, where
 * the slot size is determined by intr
 *
 * @param  tv       Input timeval to be truncated in place
 * @param  intr     Time interval in microsecs - slot width
 * @return void
 * @author veeve
 */
void hp_trunc_time(struct timeval *tv,
                   uint64          intr) {
  uint64 time_in_micro;

  /* Convert to microsecs and trunc that first */
  time_in_micro = (tv->tv_sec * 1000000) + tv->tv_usec;
  time_in_micro /= intr;
  time_in_micro *= intr;

  /* Update tv */
  tv->tv_sec  = (time_in_micro / 1000000);
  tv->tv_usec = (time_in_micro % 1000000);
}

/**
 * Get time delta in microseconds.
 */
long get_us_interval(struct timeval *start, struct timeval *end) {
  return (((end->tv_sec - start->tv_sec) * 1000000)
          + (end->tv_usec - start->tv_usec));
}

/**
 * Incr time with the given microseconds.
 */
void incr_us_interval(struct timeval *start, uint64 incr) {
  incr += (start->tv_sec * 1000000 + start->tv_usec);
  start->tv_sec  = incr/1000000;
  start->tv_usec = incr%1000000;
  return;
}

/**
 * Convert from TSC counter values to equivalent microseconds.
 *
 * @param uint64 count, TSC count value
 * @param double cpu_frequency, the CPU clock rate (MHz)
 * @return 64 bit unsigned integer
 *
 * @author cjiang
 */
inline double get_us_from_tsc(uint64 count, double cpu_frequency) {
  return count / cpu_frequency;
}

/**
 * Convert microseconds to equivalent TSC counter ticks
 *
 * @param uint64 microseconds
 * @param double cpu_frequency, the CPU clock rate (MHz)
 * @return 64 bit unsigned integer
 *
 * @author veeve
 */
inline uint64 get_tsc_from_us(uint64 usecs, double cpu_frequency) {
  return (uint64) (usecs * cpu_frequency);
}

/**
 * This is a microbenchmark to get cpu frequency the process is running on. The
 * returned value is used to convert TSC counter values to microseconds.
 *
 * @return double.
 * @author cjiang
 */
double get_cpu_frequency() {
  struct timeval start;
  struct timeval end;

  if (gettimeofday(&start, 0)) {
    perror("gettimeofday");
    return 0.0;
  }
  uint64 tsc_start = cycle_timer();
  /* Sleep for 5 miliseconds. Comparaing with gettimeofday's  few microseconds
   * execution time, this should be enough. */
  usleep(5000);
  if (gettimeofday(&end, 0)) {
    perror("gettimeofday");
    return 0.0;
  }
  uint64 tsc_end = cycle_timer();
  return (tsc_end - tsc_start) * 1.0 / (get_us_interval(&start, &end));
}


/**
 * A hash function to calculate a 8-bit hash code for a function name.
 * This is based on a small modification to 'zend_inline_hash_func' by summing
 * up all bytes of the ulong returned by 'zend_inline_hash_func'.
 *
 * @param str, char *, string to be calculated hash code for.
 *
 * @author cjiang
 */
uint8 hp_inline_hash(char * str) {
  ulong h = 5381;
  uint i = 0;
  uint8 res = 0;

  while (*str) {
    h += (h << 5);
    h ^= (ulong) *str++;
  }

  for (i = 0; i < sizeof(ulong); i++) {
    res += ((uint8 *)&h)[i];
  }
  return res;
}

/* Free this memory at the end of profiling */
void hp_array_del(char **name_array) {
  if (name_array != NULL) {
    int i = 0;
    for(; name_array[i] != NULL && i < XHPROF_MAX_IGNORED_FUNCTIONS; i++) {
      efree(name_array[i]);
    }
    efree(name_array);
  }
}

/**
 * Takes an input of the form /a/b/c/d/foo.php and returns
 * a pointer to one-level directory and basefile name
 * (d/foo.php) in the same string.
 */
const char *hp_get_base_filename(const char *filename) {
  const char *ptr;
  int   found = 0;

  if (!filename)
    return "";

  /* reverse search for "/" and return a ptr to the next char */
  for (ptr = filename + strlen(filename) - 1; ptr >= filename; ptr--) {
    if (*ptr == '/') {
      found++;
    }
    if (found == 2) {
      return ptr + 1;
    }
  }

  /* no "/" char found, so return the whole string */
  return filename;
}


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
