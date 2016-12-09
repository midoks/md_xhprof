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
  | Author: midoks(midoks@163.com)                                       |
  +----------------------------------------------------------------------+
*/
#include "php.h"

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef __FreeBSD__
# if __FreeBSD_version >= 700110
#   include <sys/resource.h>
#   include <sys/cpuset.h>
#   define cpu_set_t cpuset_t
#   define SET_AFFINITY(pid, size, mask) \
           cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, size, mask)
#   define GET_AFFINITY(pid, size, mask) \
           cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, size, mask)
# else
#   error "This version of FreeBSD does not support cpusets"
# endif /* __FreeBSD_version */
#elif __APPLE__
/*
 * Patch for compiling in Mac OS X Leopard
 * @author Svilen Spasov <s.spasov@gmail.com>
 */
#    include <mach/mach_init.h>
#    include <mach/thread_policy.h>
#    define cpu_set_t thread_affinity_policy_data_t
#    define CPU_SET(cpu_id, new_mask) \
        (*(new_mask)).affinity_tag = (cpu_id + 1)
#    define CPU_ZERO(new_mask)                 \
        (*(new_mask)).affinity_tag = THREAD_AFFINITY_TAG_NULL
#   define SET_AFFINITY(pid, size, mask)       \
        thread_policy_set(mach_thread_self(), THREAD_AFFINITY_POLICY, mask, \
                          THREAD_AFFINITY_POLICY_COUNT)
#else
/* For sched_getaffinity, sched_setaffinity */
# include <sched.h>
# define SET_AFFINITY(pid, size, mask) sched_setaffinity(0, size, mask)
# define GET_AFFINITY(pid, size, mask) sched_getaffinity(0, size, mask)
#endif /* __FreeBSD__ */

/**
 * **********************
 * GLOBAL MACRO CONSTANTS
 * **********************
 */

/* XHProf version                           */
#define XHPROF_VERSION       "0.9.2"

/* Fictitious function name to represent top of the call tree. The paranthesis
 * in the name is to ensure we don't conflict with user function names.  */
#define ROOT_SYMBOL                "main()"

/* Size of a temp scratch buffer            */
#define SCRATCH_BUF_LEN            512

/* Various XHPROF modes. If you are adding a new mode, register the appropriate
 * callbacks in hp_begin() */
#define XHPROF_MODE_HIERARCHICAL            1
#define XHPROF_MODE_SAMPLED            620002      /* Rockfort's zip code */

/* Hierarchical profiling flags.
 *
 * Note: Function call counts and wall (elapsed) time are always profiled.
 * The following optional flags can be used to control other aspects of
 * profiling.
 */
#define XHPROF_FLAGS_NO_BUILTINS   0x0001         /* do not profile builtins */
#define XHPROF_FLAGS_CPU           0x0002         /* gather CPU times for funcs */
#define XHPROF_FLAGS_MEMORY        0x0004         /* gather memory usage for funcs */

/* Constants for XHPROF_MODE_SAMPLED        */
#define XHPROF_SAMPLING_INTERVAL       100000     /* In microsecs        */

/* Constant for ignoring functions, transparent to hierarchical profile */
#define XHPROF_MAX_IGNORED_FUNCTIONS  256
#define XHPROF_IGNORED_FUNCTION_FILTER_SIZE                           \
               ((XHPROF_MAX_IGNORED_FUNCTIONS + 7)/8)

#if !defined(uint64)
typedef unsigned long long uint64;
#endif
#if !defined(uint32)
typedef unsigned int uint32;
#endif
#if !defined(uint8)
typedef unsigned char uint8;
#endif

#if !defined(ulong)
#define ulong unsigned long
#endif

/* Bloom filter for function names to be ignored */
#define INDEX_2_BYTE(index)  (index >> 3)
#define INDEX_2_BIT(index)   (1 << (index & 0x7));

uint64 cycle_timer();
void hp_trunc_time(struct timeval *tv,uint64 intr);

double get_cpu_frequency();

long get_us_interval(struct timeval *start, struct timeval *end);
void incr_us_interval(struct timeval *start, uint64 incr);
double get_us_from_tsc(uint64 count, double cpu_frequency);
uint64 get_tsc_from_us(uint64 usecs, double cpu_frequency);

uint8 hp_inline_hash(char * str);
void hp_array_del(char **name_array);
const char *hp_get_base_filename(const char *filename);

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
