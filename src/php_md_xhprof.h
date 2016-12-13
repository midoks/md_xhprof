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


/* $Id$ */

#ifndef PHP_MD_XHPROF_H
#define PHP_MD_XHPROF_H

extern zend_module_entry md_xhprof_module_entry;
#define phpext_md_xhprof_ptr &md_xhprof_module_entry

#define PHP_MD_XHPROF_VERSION "0.1.0" /* Replace with version number for your extension */

#ifdef PHP_WIN32
#	define PHP_MD_XHPROF_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#	define PHP_MD_XHPROF_API __attribute__ ((visibility("default")))
#else
#	define PHP_MD_XHPROF_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

/*
  	Declare any global variables you may need between the BEGIN
	and END macros here:

ZEND_BEGIN_MODULE_GLOBALS(md_xhprof)
	zend_long  global_value;
	char *global_string;
ZEND_END_MODULE_GLOBALS(md_xhprof)
*/

/* Always refer to the globals in your function as MD_XHPROF_G(variable).
   You are encouraged to rename these macros something shorter, see
   examples in any other php module directory.
*/
#define MD_XHPROF_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(md_xhprof, v)

#if defined(ZTS) && defined(COMPILE_DL_MD_XHPROF)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#endif	/* PHP_MD_XHPROF_H */

PHP_FUNCTION(xhprof_enable);
PHP_FUNCTION(xhprof_disable);
PHP_FUNCTION(xhprof_sample_enable);
PHP_FUNCTION(xhprof_sample_disable);




/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
