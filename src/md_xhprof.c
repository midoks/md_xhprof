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

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/standard/php_var.h"

#include "php_md_xhprof.h"
#include "xhprof.h"


/**
 * *****************************
 * GLOBAL DATATYPES AND TYPEDEFS
 * *****************************
 */

/* XHProf maintains a stack of entries being profiled. The memory for the entry
 * is passed by the layer that invokes BEGIN_PROFILING(), e.g. the hp_execute()
 * function. Often, this is just C-stack memory.
 *
 * This structure is a convenient place to track start time of a particular
 * profile operation, recursion depth, and the name of the function being
 * profiled. */
typedef struct hp_entry_t {
  char                   *name_hprof;                       /* function name */
  int                     rlvl_hprof;        /* recursion level for function */
  uint64                  tsc_start;         /* start value for TSC counter  */
  long int                mu_start_hprof;                    /* memory usage */
  long int                pmu_start_hprof;              /* peak memory usage */
  struct rusage           ru_start_hprof;             /* user/sys time start */
  struct hp_entry_t      *prev_hprof;    /* ptr to prev entry being profiled */
  uint8                   hash_code;     /* hash_code for the function name  */
} hp_entry_t;

/* Various types for XHPROF callbacks       */
typedef void (*hp_init_cb)           (TSRMLS_D);
typedef void (*hp_exit_cb)           (TSRMLS_D);
typedef void (*hp_begin_function_cb) (hp_entry_t **entries,
                                      hp_entry_t *current   TSRMLS_DC);
typedef void (*hp_end_function_cb)   (hp_entry_t **entries  TSRMLS_DC);

/* Struct to hold the various callbacks for a single xhprof mode */
typedef struct hp_mode_cb {
  hp_init_cb             init_cb;
  hp_exit_cb             exit_cb;
  hp_begin_function_cb   begin_fn_cb;
  hp_end_function_cb     end_fn_cb;
} hp_mode_cb;

/* Xhprof's global state.
 *
 * This structure is instantiated once.  Initialize defaults for attributes in
 * hp_init_profiler_state() Cleanup/free attributes in
 * hp_clean_profiler_state() */
typedef struct hp_global_t {

  /*       ----------   Global attributes:  -----------       */

  /* Indicates if xhprof is currently enabled */
  int              enabled;

  /* Indicates if xhprof was ever enabled during this request */
  int              ever_enabled;

  /* Holds all the xhprof statistics */
  zval             stats_count;

  /* Indicates the current xhprof mode or level */
  int              profiler_level;

  /* Top of the profile stack */
  hp_entry_t      *entries;

  /* freelist of hp_entry_t chunks for reuse... */
  hp_entry_t      *entry_free_list;

  /* Callbacks for various xhprof modes */
  hp_mode_cb       mode_cb;

  /*       ----------   Mode specific attributes:  -----------       */

  /* Global to track the time of the last sample in time and ticks */
  struct timeval   last_sample_time;
  uint64           last_sample_tsc;
  /* XHPROF_SAMPLING_INTERVAL in ticks */
  uint64           sampling_interval_tsc;

  /* This array is used to store cpu frequencies for all available logical
   * cpus.  For now, we assume the cpu frequencies will not change for power
   * saving or other reasons. If we need to worry about that in the future, we
   * can use a periodical timer to re-calculate this arrary every once in a
   * while (for example, every 1 or 5 seconds). */
  double *cpu_frequencies;

  /* The number of logical CPUs this machine has. */
  uint32 cpu_num;

  /* The saved cpu affinity. */
  cpu_set_t prev_mask;

  /* The cpu id current process is bound to. (default 0) */
  uint32 cur_cpu_id;

  /* XHProf flags */
  uint32 xhprof_flags;

  /* counter table indexed by hash value of function names. */
  uint8  func_hash_counters[256];

  /* Table of ignored function names and their filter */
  char  **ignored_function_names;
  uint8   ignored_function_filter[XHPROF_IGNORED_FUNCTION_FILTER_SIZE];

} hp_global_t;


/**
 * ***********************
 * GLOBAL STATIC VARIABLES
 * ***********************
 */
/* XHProf global state */
static hp_global_t       hp_globals;


/* Pointer to the original execute function */
static void (*_zend_execute_ex) (zend_execute_data *execute_data TSRMLS_DC);

/* Pointer to the origianl execute_internal function */
static void (*_zend_execute_internal) (zend_execute_data *execute_data, zval *return_value TSRMLS_DC);

/* Pointer to the original compile function */
static zend_op_array * (*_zend_compile_file) (zend_file_handle *file_handle, int type TSRMLS_DC);

/* Pointer to the original compile string function (used by eval) */
static zend_op_array * (*_zend_compile_string) (zval *source_string, char *filename TSRMLS_DC);

/* Bloom filter for function names to be ignored */
#define INDEX_2_BYTE(index)  (index >> 3)
#define INDEX_2_BIT(index)   (1 << (index & 0x7));


/**
 * ****************************
 * STATIC FUNCTION DECLARATIONS
 * ****************************
 */
static void hp_register_constants(INIT_FUNC_ARGS);

static void hp_begin(long level, long xhprof_flags TSRMLS_DC);
static void hp_stop(TSRMLS_D);
static void hp_end(TSRMLS_D);

static void clear_frequencies();

static void hp_free_the_free_list();
static hp_entry_t *hp_fast_alloc_hprof_entry();
static void hp_fast_free_hprof_entry(hp_entry_t *p);
static void get_all_cpu_frequencies();

static void hp_get_ignored_functions_from_arg(zval *args);
static void hp_ignored_functions_filter_clear();
static void hp_ignored_functions_filter_init();

static inline zval  *hp_zval_at_key(char  *key, zval  *values);
static inline char **hp_strings_in_zval(zval  *values);
int restore_cpu_affinity(cpu_set_t * prev_mask);
int bind_to_cpu(uint32 cpu_id);


static void hp_print_zstr(zend_string *key);


///定义方法实现
//////////////////////////
//////////////////////////

static void hp_print_zstr(zend_string *key){
  php_printf("<!--[zend_string](");
  PHPWRITE(ZSTR_VAL(key), ZSTR_LEN(key));
  php_printf(")-->\n");
}


/**
 * ***************************************************
 * COMMON HELPER FUNCTION DEFINITIONS AND LOCAL MACROS
 * ***************************************************
 */

static void hp_register_constants(INIT_FUNC_ARGS) {
  REGISTER_LONG_CONSTANT("XHPROF_FLAGS_NO_BUILTINS",
                         XHPROF_FLAGS_NO_BUILTINS,
                         CONST_CS | CONST_PERSISTENT);

  REGISTER_LONG_CONSTANT("XHPROF_FLAGS_CPU",
                         XHPROF_FLAGS_CPU,
                         CONST_CS | CONST_PERSISTENT);

  REGISTER_LONG_CONSTANT("XHPROF_FLAGS_MEMORY",
                         XHPROF_FLAGS_MEMORY,
                         CONST_CS | CONST_PERSISTENT);
}

/**
 * Parse the list of ignored functions from the zval argument.
 *
 * @author mpal
 */
static void hp_get_ignored_functions_from_arg(zval *args) {

  if (hp_globals.ignored_function_names) {
    hp_array_del(hp_globals.ignored_function_names);
  }

  if (args != NULL) {
    zval  *zresult = NULL;

    zresult = hp_zval_at_key("ignored_functions", args);
    hp_globals.ignored_function_names = hp_strings_in_zval(zresult);
  } else {
    hp_globals.ignored_function_names = NULL;
  }
}

/**
 * Clear filter for functions which may be ignored during profiling.
 *
 * @author mpal
 */
static void hp_ignored_functions_filter_clear() {
  memset(hp_globals.ignored_function_filter, 0,
         XHPROF_IGNORED_FUNCTION_FILTER_SIZE);
}

/**
 * Initialize filter for ignored functions using bit vector.
 *
 * @author mpal
 */
static void hp_ignored_functions_filter_init() {
  if (hp_globals.ignored_function_names != NULL) {
    int i = 0;
    for(; hp_globals.ignored_function_names[i] != NULL; i++) {
      char *str  = hp_globals.ignored_function_names[i];
      uint8 hash = hp_inline_hash(str);
      int   idx  = INDEX_2_BYTE(hash);
      hp_globals.ignored_function_filter[idx] |= INDEX_2_BIT(hash);
    }
  }
}

/**
 * Check if function collides in filter of functions to be ignored.
 *
 * @author mpal
 */
int hp_ignored_functions_filter_collision(uint8 hash) {
  uint8 mask = INDEX_2_BIT(hash);
  return hp_globals.ignored_function_filter[INDEX_2_BYTE(hash)] & mask;
}

/**
 * Initialize profiler state
 *
 * @author kannan, veeve
 */
void hp_init_profiler_state(int level TSRMLS_DC) {
  /* Setup globals */
  if (!hp_globals.ever_enabled) {
    hp_globals.ever_enabled  = 1;
    hp_globals.entries = NULL;
  }
  hp_globals.profiler_level  = (int) level;

  /* Init stats_count */
  array_init(&hp_globals.stats_count);

  /* NOTE(cjiang): some fields such as cpu_frequencies take relatively longer
   * to initialize, (5 milisecond per logical cpu right now), therefore we
   * calculate them lazily. */
  if (hp_globals.cpu_frequencies == NULL) {
    get_all_cpu_frequencies();
    restore_cpu_affinity(&hp_globals.prev_mask);
  }

  /* bind to a random cpu so that we can use rdtsc instruction. */
  bind_to_cpu((int) (rand() % hp_globals.cpu_num));

  /* Call current mode's init cb */
  hp_globals.mode_cb.init_cb(TSRMLS_C);

  /* Set up filter of functions which may be ignored during profiling */
  hp_ignored_functions_filter_init();
}

/**
 * Cleanup profiler state
 *
 * @author kannan, veeve
 */
void hp_clean_profiler_state(TSRMLS_D) {
  /* Call current mode's exit cb */
  hp_globals.mode_cb.exit_cb(TSRMLS_C);

  /* Clear globals */
  array_init(&hp_globals.stats_count);

  hp_globals.entries = NULL;
  hp_globals.profiler_level = 1;
  hp_globals.ever_enabled = 0;

  /* Delete the array storing ignored function names */
  hp_array_del(hp_globals.ignored_function_names);
  hp_globals.ignored_function_names = NULL;
}

/*
 * Start profiling - called just before calling the actual function
 * NOTE:  PLEASE MAKE SURE TSRMLS_CC IS AVAILABLE IN THE CONTEXT
 *        OF THE FUNCTION WHERE THIS MACRO IS CALLED.
 *        TSRMLS_CC CAN BE MADE AVAILABLE VIA TSRMLS_DC IN THE
 *        CALLING FUNCTION OR BY CALLING TSRMLS_FETCH()
 *        TSRMLS_FETCH() IS RELATIVELY EXPENSIVE.
 */
#define BEGIN_PROFILING(entries, symbol, profile_curr)                  \
  do {                                                                  \
    /* Use a hash code to filter most of the string comparisons. */     \
    uint8 hash_code  = hp_inline_hash(symbol);                          \
    profile_curr = !hp_ignore_entry(hash_code, symbol);                 \
    if (profile_curr) {                                                 \
      hp_entry_t *cur_entry = hp_fast_alloc_hprof_entry();              \
      (cur_entry)->hash_code = hash_code;                               \
      (cur_entry)->name_hprof = symbol;                                 \
      (cur_entry)->prev_hprof = (*(entries));                           \
      /* Call the universal callback */                                 \
      hp_mode_common_beginfn((entries), (cur_entry) TSRMLS_CC);         \
      /* Call the mode's beginfn callback */                            \
      hp_globals.mode_cb.begin_fn_cb((entries), (cur_entry) TSRMLS_CC); \
      /* Update entries linked list */                                  \
      (*(entries)) = (cur_entry);                                       \
    }                                                                   \
  } while (0)



/*
 * Stop profiling - called just after calling the actual function
 * NOTE:  PLEASE MAKE SURE TSRMLS_CC IS AVAILABLE IN THE CONTEXT
 *        OF THE FUNCTION WHERE THIS MACRO IS CALLED.
 *        TSRMLS_CC CAN BE MADE AVAILABLE VIA TSRMLS_DC IN THE
 *        CALLING FUNCTION OR BY CALLING TSRMLS_FETCH()
 *        TSRMLS_FETCH() IS RELATIVELY EXPENSIVE.
 */
#define END_PROFILING(entries, profile_curr)                            \
  do {                                                                  \
    if (profile_curr) {                                                 \
      hp_entry_t *cur_entry;                                            \
      /* Call the mode's endfn callback. */                             \
      /* NOTE(cjiang): we want to call this 'end_fn_cb' before */       \
      /* 'hp_mode_common_endfn' to avoid including the time in */       \
      /* 'hp_mode_common_endfn' in the profiling results.      */       \
      hp_globals.mode_cb.end_fn_cb((entries) TSRMLS_CC);                \
      cur_entry = (*(entries));                                         \
      /* Call the universal callback */                                 \
      hp_mode_common_endfn((entries), (cur_entry) TSRMLS_CC);           \
      /* Free top entry and update entries linked list */               \
      (*(entries)) = (*(entries))->prev_hprof;                          \
      hp_fast_free_hprof_entry(cur_entry);                              \
    }                                                                   \
  } while (0)


/**
 * Returns formatted function name
 *
 * @param  entry        hp_entry
 * @param  result_buf   ptr to result buf
 * @param  result_len   max size of result buf
 * @return total size of the function name returned in result_buf
 * @author veeve
 */
size_t hp_get_entry_name(hp_entry_t  *entry,
                         char           *result_buf,
                         size_t          result_len) {



  /* Validate result_len */
  if (result_len <= 1) {
    /* Insufficient result_bug. Bail! */
    return 0;
  }

  /* Add '@recurse_level' if required */
  /* NOTE:  Dont use snprintf's return val as it is compiler dependent */
  if (entry->rlvl_hprof) {
    snprintf(result_buf, result_len,
             "%s@%d",
             entry->name_hprof, entry->rlvl_hprof);
  }
  else {
    snprintf(result_buf, result_len,
             "%s",
             entry->name_hprof);
  }

  // php_printf("hp_get_entry_name -- end :%s\n",result_buf);

  /* Force null-termination at MAX */
  result_buf[result_len - 1] = 0;

  return strlen(result_buf);
}

/**
 * Check if this entry should be ignored, first with a conservative Bloomish
 * filter then with an exact check against the function names.
 *
 * @author mpal
 */
int  hp_ignore_entry_work(uint8 hash_code, char *curr_func) {
  int ignore = 0;
  if (hp_ignored_functions_filter_collision(hash_code)) {
    int i = 0;
    for (; hp_globals.ignored_function_names[i] != NULL; i++) {
      char *name = hp_globals.ignored_function_names[i];
      if ( !strcmp(curr_func, name)) {
        ignore++;
        break;
      }
    }
  }

  return ignore;
}

static inline int  hp_ignore_entry(uint8 hash_code, char *curr_func) {
  /* First check if ignoring functions is enabled */
  return hp_globals.ignored_function_names != NULL &&
         hp_ignore_entry_work(hash_code, curr_func);
}

/**
 * Build a caller qualified name for a callee.
 *
 * For example, if A() is caller for B(), then it returns "A==>B".
 * Recursive invokations are denoted with @<n> where n is the recursion
 * depth.
 *
 * For example, "foo==>foo@1", and "foo@2==>foo@3" are examples of direct
 * recursion. And  "bar==>foo@1" is an example of an indirect recursive
 * call to foo (implying the foo() is on the call stack some levels
 * above).
 *
 * @author kannan, veeve
 */
size_t hp_get_function_stack(hp_entry_t *entry,
                             int            level,
                             char          *result_buf,
                             size_t         result_len) {
  size_t         len = 0;

  /* End recursion if we dont need deeper levels or we dont have any deeper
   * levels */
  if (!entry->prev_hprof || (level <= 1)) {
    return hp_get_entry_name(entry, result_buf, result_len);
  }

  /* Take care of all ancestors first */
  len = hp_get_function_stack(entry->prev_hprof,
                              level - 1,
                              result_buf,
                              result_len);

  /* Append the delimiter */
# define    HP_STACK_DELIM        "==>"
# define    HP_STACK_DELIM_LEN    (sizeof(HP_STACK_DELIM) - 1)

  if (result_len < (len + HP_STACK_DELIM_LEN)) {
    /* Insufficient result_buf. Bail out! */
    return len;
  }

  /* Add delimiter only if entry had ancestors */
  if (len) {
    strncat(result_buf + len,
            HP_STACK_DELIM,
            result_len - len);
    len += HP_STACK_DELIM_LEN;
  }

# undef     HP_STACK_DELIM_LEN
# undef     HP_STACK_DELIM

  /* Append the current function name */
  return len + hp_get_entry_name(entry,
                                 result_buf + len,
                                 result_len - len);
}


/**
 * Get the name of the current function. The name is qualified with
 * the class name if the function is in a class.
 *
 * @author kannan, hzhao
 */
static char *hp_get_function_name(zend_op_array *ops TSRMLS_DC) {
  zend_execute_data *data;

  zend_function      *curr_func = NULL;
  zend_string        *func  = NULL;
  zend_string        *cls   = NULL;
  char               *ret   = NULL;
  int                len;

  data = EG(current_execute_data);

  if (data) {
    /* shared meta data for function on the call stack */
    curr_func = data->func;

    /* extract function name from the meta info */
    func = curr_func->common.function_name;

    if (func) {
      /* previously, the order of the tests in the "if" below was
       * flipped, leading to incorrect function names in profiler
       * reports. When a method in a super-type is invoked the
       * profiler should qualify the function name with the super-type
       * class name (not the class name based on the run-time type
       * of the object.
       */
     
      if (curr_func->common.scope) {
        cls = curr_func->common.scope->name;
      } else if (data->called_scope) {
        cls = data->called_scope->name;
      }

      if ( cls ) {

        len = ZSTR_LEN(cls) + ZSTR_LEN(func)+3;
        ret = (char*)emalloc(len);
        snprintf(ret, len, "%s::%s", ZSTR_VAL(cls), ZSTR_VAL(func));

      } else {

        len = ZSTR_LEN(func)+1;
        ret = (char*)emalloc(len);
        snprintf(ret, len, "%s", ZSTR_VAL(func));
      }

    } else {

      long     curr_op;
      int      add_filename = 0;

      const char        *_func = NULL;

      /* we are dealing with a special directive/function like
       * include, eval, etc.
       */
      if (data->prev_execute_data) {
        curr_op = data->prev_execute_data->opline->extended_value;
      } else {
        curr_op = data->opline->extended_value;
      }

      switch (curr_op) {
        case ZEND_EVAL:
          _func = "eval";
          break;
        case ZEND_INCLUDE:
          _func = "include";
          add_filename = 1;
          break;
        case ZEND_REQUIRE:
          _func = "require";
          add_filename = 1;
          break;
        case ZEND_INCLUDE_ONCE:
          _func = "include_once";
          add_filename = 1;
          break;
        case ZEND_REQUIRE_ONCE:
          _func = "require_once";
          add_filename = 1;
          break;
        default:
          _func = "???_op";
          break;
      }

      /* For some operations, we'll add the filename as part of the function
       * name to make the reports more useful. So rather than just "include"
       * you'll see something like "run_init::foo.php" in your reports.
       */
      if (add_filename){
        const char *filename;
        int   len;
        filename = hp_get_base_filename((curr_func->op_array).filename->val);
        len      = strlen("run_init") + strlen(filename) + 3;
        ret      = (char *)emalloc(len);
        snprintf(ret, len, "run_init::%s", filename);
      } else {
        ret = estrdup(_func);
      }
    }
  }
  return ret;
}

/**
 * Free any items in the free list.
 */
static void hp_free_the_free_list() {
  hp_entry_t *p = hp_globals.entry_free_list;
  hp_entry_t *cur = NULL;

  while (p) {
    cur = p;
    p = p->prev_hprof;
    free(cur);
  }
}

/**
 * Fast allocate a hp_entry_t structure. Picks one from the
 * free list if available, else does an actual allocate.
 *
 * Doesn't bother initializing allocated memory.
 *
 * @author kannan
 */
static hp_entry_t *hp_fast_alloc_hprof_entry() {
  hp_entry_t *p;

  p = hp_globals.entry_free_list;

  if (p) {
    hp_globals.entry_free_list = p->prev_hprof;
    return p;
  } else {
    return (hp_entry_t *)malloc(sizeof(hp_entry_t));
  }
}

/**
 * Fast free a hp_entry_t structure. Simply returns back
 * the hp_entry_t to a free list and doesn't actually
 * perform the free.
 *
 * @author kannan
 */
static void hp_fast_free_hprof_entry(hp_entry_t *p) {

  /* we use/overload the prev_hprof field in the structure to link entries in
   * the free list. */
  p->prev_hprof = hp_globals.entry_free_list;
  hp_globals.entry_free_list = p;
}

/**
 * Increment the count of the given stat with the given count
 * If the stat was not set before, inits the stat to the given count
 *
 * @param  zval *counts   Zend hash table pointer
 * @param  char *name     Name of the stat
 * @param  long  count    Value of the stat to incr by
 * @return void
 * @author kannan
 */
void hp_inc_count(zval *counts, char *name, long count TSRMLS_DC) {

  HashTable *ht;
  zval *data;

  if (!counts) return;

  zend_string *key = zend_string_init(name, strlen(name), 0);
  ht = Z_ARRVAL_P(counts);
  if(0 == ht->nNumOfElements){

    add_assoc_long(counts, "ct", 1);
  } else {

    if(zend_hash_exists(ht, key)){

      data = zend_hash_find(ht, key);
      // php_printf("name:%s\n",name);
      // php_printf("value:%d\n", Z_LVAL_P(data));

      add_assoc_long(counts, name, Z_LVAL_P(data) + count);
    } else {

      add_assoc_long(counts, name, count);
    }
  }
}

/**
 * Looksup the hash table for the given symbol
 * Initializes a new array() if symbol is not present
 *
 * @author kannan, veeve
 */
zval * hp_hash_lookup(char *symbol  TSRMLS_DC) {
  HashTable    *ht  = NULL;
  zval        *counts_p = NULL;
  static zval  hp_globals_counts;

  /* Lookup our hash table */
  ht = Z_ARRVAL_P(&hp_globals.stats_count);
  counts_p = &hp_globals_counts;
  if(0 == ht->nNumOfElements){

    array_init(&hp_globals_counts);
    add_assoc_zval(&hp_globals.stats_count, symbol, counts_p);
  } else {

    zend_string *key = zend_string_init(symbol, strlen(symbol), 1);
    if(zend_hash_exists(ht, key)){
      counts_p = zend_hash_find(ht, key);
    } else {
      array_init(&hp_globals_counts);
      add_assoc_zval(&hp_globals.stats_count, symbol, &hp_globals_counts);
    }
  }

  //return (zval *) 0;
  return counts_p;
}

/**
 * Sample the stack. Add it to the stats_count global.
 *
 * @param  tv            current time
 * @param  entries       func stack as linked list of hp_entry_t
 * @return void
 * @author veeve
 */
void hp_sample_stack(hp_entry_t  **entries  TSRMLS_DC) {
  char key[SCRATCH_BUF_LEN];
  char symbol[SCRATCH_BUF_LEN * 1000];

  /* Build key */
  snprintf(key, sizeof(key),
           "%d.%06d",
           hp_globals.last_sample_time.tv_sec,
           hp_globals.last_sample_time.tv_usec);

  /* Init stats in the global stats_count hashtable */
  hp_get_function_stack(*entries,
                        INT_MAX,
                        symbol,
                        sizeof(symbol));

  add_assoc_string(&hp_globals.stats_count,
                   key,
                   symbol);
  return;
}

/**
 * Checks to see if it is time to sample the stack.
 * Calls hp_sample_stack() if its time.
 *
 * @param  entries        func stack as linked list of hp_entry_t
 * @param  last_sample    time the last sample was taken
 * @param  sampling_intr  sampling interval in microsecs
 * @return void
 * @author veeve
 */
void hp_sample_check(hp_entry_t **entries  TSRMLS_DC) {
  /* Validate input */
  if (!entries || !(*entries)) {
    return;
  }

  /* See if its time to sample.  While loop is to handle a single function
   * taking a long time and passing several sampling intervals. */
  while ((cycle_timer() - hp_globals.last_sample_tsc)
         > hp_globals.sampling_interval_tsc) {

    /* bump last_sample_tsc */
    hp_globals.last_sample_tsc += hp_globals.sampling_interval_tsc;

    /* bump last_sample_time - HAS TO BE UPDATED BEFORE calling hp_sample_stack */
    incr_us_interval(&hp_globals.last_sample_time, XHPROF_SAMPLING_INTERVAL);

    /* sample the stack */
    hp_sample_stack(entries  TSRMLS_CC);
  }

  return;
}



/**
 * Bind the current process to a specified CPU. This function is to ensure that
 * the OS won't schedule the process to different processors, which would make
 * values read by rdtsc unreliable.
 *
 * @param uint32 cpu_id, the id of the logical cpu to be bound to.
 * @return int, 0 on success, and -1 on failure.
 *
 * @author cjiang
 */
int bind_to_cpu(uint32 cpu_id) {
  cpu_set_t new_mask;

  CPU_ZERO(&new_mask);
  CPU_SET(cpu_id, &new_mask);

  // if (SET_AFFINITY(0, sizeof(cpu_set_t), &new_mask) < 0) {
  //   perror("setaffinity");
  //   return -1;
  // }

  /* record the cpu_id the process is bound to. */
  hp_globals.cur_cpu_id = cpu_id;

  return 0;
}


/**
 * Calculate frequencies for all available cpus.
 *
 * @author cjiang
 */
static void get_all_cpu_frequencies() {
  int id;
  double frequency;

  hp_globals.cpu_frequencies = malloc(sizeof(double) * hp_globals.cpu_num);
  if (hp_globals.cpu_frequencies == NULL) {
    return;
  }

  /* Iterate over all cpus found on the machine. */
  for (id = 0; id < hp_globals.cpu_num; ++id) {
    /* Only get the previous cpu affinity mask for the first call. */
    if (bind_to_cpu(id)) {
      clear_frequencies();
      return;
    }

    /* Make sure the current process gets scheduled to the target cpu. This
     * might not be necessary though. */
    usleep(0);

    frequency = get_cpu_frequency();
    if (frequency == 0.0) {
      clear_frequencies();
      return;
    }
    hp_globals.cpu_frequencies[id] = frequency;
  }
}

/**
 * Restore cpu affinity mask to a specified value. It returns 0 on success and
 * -1 on failure.
 *
 * @param cpu_set_t * prev_mask, previous cpu affinity mask to be restored to.
 * @return int, 0 on success, and -1 on failure.
 *
 * @author cjiang
 */
int restore_cpu_affinity(cpu_set_t * prev_mask) {

  // if (SET_AFFINITY(0, sizeof(cpu_set_t), prev_mask) < 0) {
  //   perror("restore setaffinity");
  //   return -1;
  // }

  /* default value ofor cur_cpu_id is 0. */
  hp_globals.cur_cpu_id = 0;
  return 0;
}

/**
 * Reclaim the memory allocated for cpu_frequencies.
 *
 * @author cjiang
 */
static void clear_frequencies() {

  if (hp_globals.cpu_frequencies) {
    free(hp_globals.cpu_frequencies);
    hp_globals.cpu_frequencies = NULL;
  }

  restore_cpu_affinity(&hp_globals.prev_mask);
}


/**
 * ***************************
 * XHPROF DUMMY CALLBACKS
 * ***************************
 */
void hp_mode_dummy_init_cb(TSRMLS_D) { }


void hp_mode_dummy_exit_cb(TSRMLS_D) { }


void hp_mode_dummy_beginfn_cb(hp_entry_t **entries,
                              hp_entry_t *current  TSRMLS_DC) { }

void hp_mode_dummy_endfn_cb(hp_entry_t **entries   TSRMLS_DC) { }


/**
 * ****************************
 * XHPROF COMMON CALLBACKS
 * ****************************
 */
/**
 * XHPROF universal begin function.
 * This function is called for all modes before the
 * mode's specific begin_function callback is called.
 *
 * @param  hp_entry_t **entries  linked list (stack)
 *                                  of hprof entries
 * @param  hp_entry_t  *current  hprof entry for the current fn
 * @return void
 * @author kannan, veeve
 */
void hp_mode_common_beginfn(hp_entry_t **entries,
                            hp_entry_t  *current  TSRMLS_DC) {
  hp_entry_t   *p;

  /* This symbol's recursive level */
  int    recurse_level = 0;

  if (hp_globals.func_hash_counters[current->hash_code] > 0) {
    /* Find this symbols recurse level */
    for(p = (*entries); p; p = p->prev_hprof) {
      if (!strcmp(current->name_hprof, p->name_hprof)) {
        recurse_level = (p->rlvl_hprof) + 1;
        break;
      }
    }
  }
  hp_globals.func_hash_counters[current->hash_code]++;

  /* Init current function's recurse level */
  current->rlvl_hprof = recurse_level;

}

/**
 * XHPROF universal end function.  This function is called for all modes after
 * the mode's specific end_function callback is called.
 *
 * @param  hp_entry_t **entries  linked list (stack) of hprof entries
 * @return void
 * @author kannan, veeve
 */
void hp_mode_common_endfn(hp_entry_t **entries, hp_entry_t *current TSRMLS_DC) {
  hp_globals.func_hash_counters[current->hash_code]--;
}


/**
 * *********************************
 * XHPROF INIT MODULE CALLBACKS
 * *********************************
 */
/**
 * XHPROF_MODE_SAMPLED's init callback
 *
 * @author veeve
 */
void hp_mode_sampled_init_cb(TSRMLS_D) {

  struct timeval  now;
  uint64 truncated_us;
  uint64 truncated_tsc;
  double cpu_freq = hp_globals.cpu_frequencies[hp_globals.cur_cpu_id];

  /* Init the last_sample in tsc */
  hp_globals.last_sample_tsc = cycle_timer();

  /* Find the microseconds that need to be truncated */
  gettimeofday(&hp_globals.last_sample_time, 0);
  now = hp_globals.last_sample_time;
  hp_trunc_time(&hp_globals.last_sample_time, XHPROF_SAMPLING_INTERVAL);

  /* Subtract truncated time from last_sample_tsc */
  truncated_us  = get_us_interval(&hp_globals.last_sample_time, &now);
  truncated_tsc = get_tsc_from_us(truncated_us, cpu_freq);
  if (hp_globals.last_sample_tsc > truncated_tsc) {
    /* just to be safe while subtracting unsigned ints */
    hp_globals.last_sample_tsc -= truncated_tsc;
  }

  /* Convert sampling interval to ticks */
  hp_globals.sampling_interval_tsc =
    get_tsc_from_us(XHPROF_SAMPLING_INTERVAL, cpu_freq);

}


/**
 * ************************************
 * XHPROF BEGIN FUNCTION CALLBACKS
 * ************************************
 */

/**
 * XHPROF_MODE_HIERARCHICAL's begin function callback
 *
 * @author kannan
 */
void hp_mode_hier_beginfn_cb(hp_entry_t **entries,
                             hp_entry_t  *current  TSRMLS_DC) {
  /* Get start tsc counter */
  current->tsc_start = cycle_timer();

  /* Get CPU usage */
  if (hp_globals.xhprof_flags & XHPROF_FLAGS_CPU) {
    getrusage(RUSAGE_SELF, &(current->ru_start_hprof));
  }

  /* Get memory usage */
  if (hp_globals.xhprof_flags & XHPROF_FLAGS_MEMORY) {
    current->mu_start_hprof  = zend_memory_usage(0 TSRMLS_CC);
    current->pmu_start_hprof = zend_memory_peak_usage(0 TSRMLS_CC);
  }
}


/**
 * XHPROF_MODE_SAMPLED's begin function callback
 *
 * @author veeve
 */
void hp_mode_sampled_beginfn_cb(hp_entry_t **entries,
                                hp_entry_t  *current  TSRMLS_DC) {
  /* See if its time to take a sample */
  hp_sample_check(entries  TSRMLS_CC);
}


/**
 * **********************************
 * XHPROF END FUNCTION CALLBACKS
 * **********************************
 */

/**
 * XHPROF shared end function callback
 *
 * @author kannan
 */
zval * hp_mode_shared_endfn_cb(hp_entry_t *top, char *symbol  TSRMLS_DC) {
  zval    *counts;
  uint64   tsc_end;

  /* Get end tsc counter */
  tsc_end = cycle_timer();

  /* Get the stat array */
  if (!(counts = hp_hash_lookup(symbol TSRMLS_CC))) {
    return (zval *) 0;
  }

  /* Bump stats in the counts hashtable */
  hp_inc_count(counts, "ct", 1  TSRMLS_CC);

  hp_inc_count(counts, "wt", get_us_from_tsc(tsc_end - top->tsc_start,
        hp_globals.cpu_frequencies[hp_globals.cur_cpu_id]) TSRMLS_CC);

  return counts;
}

/**
 * XHPROF_MODE_HIERARCHICAL's end function callback
 *
 * @author kannan
 */
void hp_mode_hier_endfn_cb(hp_entry_t **entries  TSRMLS_DC) {

  hp_entry_t   *top = (*entries);
  zval            *counts;
  struct rusage    ru_end;

  
  char             symbol[SCRATCH_BUF_LEN];
  long int         mu_end;
  long int         pmu_end;
  

  /* Get the stat array */
  hp_get_function_stack(top, 2, symbol, sizeof(symbol));
  if (!(counts = hp_mode_shared_endfn_cb(top,  symbol  TSRMLS_CC))) {
    return;
  }

  if (hp_globals.xhprof_flags & XHPROF_FLAGS_CPU) {
    /* Get CPU usage */
    getrusage(RUSAGE_SELF, &ru_end);

    /* Bump CPU stats in the counts hashtable */
    hp_inc_count(counts, "cpu", (get_us_interval(&(top->ru_start_hprof.ru_utime),
                                              &(ru_end.ru_utime)) +
                              get_us_interval(&(top->ru_start_hprof.ru_stime),
                                              &(ru_end.ru_stime)))
              TSRMLS_CC);
  }

  if (hp_globals.xhprof_flags & XHPROF_FLAGS_MEMORY) {
    /* Get Memory usage */
    mu_end  = zend_memory_usage(0 TSRMLS_CC);
    pmu_end = zend_memory_peak_usage(0 TSRMLS_CC);

    /* Bump Memory stats in the counts hashtable */
    hp_inc_count(counts, "mu",  mu_end - top->mu_start_hprof    TSRMLS_CC);
    hp_inc_count(counts, "pmu", pmu_end - top->pmu_start_hprof  TSRMLS_CC);
  }
}

/**
 * XHPROF_MODE_SAMPLED's end function callback
 *
 * @author veeve
 */
void hp_mode_sampled_endfn_cb(hp_entry_t **entries  TSRMLS_DC) {
  /* See if its time to take a sample */
  hp_sample_check(entries  TSRMLS_CC);
}


/**
 * ***************************
 * PHP EXECUTE/COMPILE PROXIES
 * ***************************
 */

/**
 * XHProf enable replaced the zend_execute function with this
 * new execute function. We can do whatever profiling we need to
 * before and after calling the actual zend_execute().
 *
 * @author hzhao, kannan
 */
ZEND_DLEXPORT void hp_execute_ex (zend_execute_data *execute_data TSRMLS_DC) {

  zend_op_array *ops  = &execute_data->func->op_array;
  char          *func = NULL;
  int hp_profile_flag = 1;

  func = hp_get_function_name(ops TSRMLS_CC);
  if (!func) {
    _zend_execute_ex(execute_data TSRMLS_CC);
    return;
  }

  BEGIN_PROFILING(&hp_globals.entries, func, hp_profile_flag);
  _zend_execute_ex(execute_data TSRMLS_CC);

  if (hp_globals.entries) {
    END_PROFILING(&hp_globals.entries, hp_profile_flag);
  }

  efree(func);
}

#undef EX
#define EX(element) ((execute_data)->element)

/**
 * Very similar to hp_execute. Proxy for zend_execute_internal().
 * Applies to zend builtin functions.
 *
 * @author hzhao, kannan
 */

//#define EX_T(offset) (*EX_TMP_VAR(execute_data, offset))

ZEND_DLEXPORT void hp_execute_internal(zend_execute_data *execute_data, zval *return_value TSRMLS_DC ) {

  zend_execute_data *current_data;
  char             *func = NULL;
  int    hp_profile_flag = 1;

  current_data = EG(current_execute_data);
  func = hp_get_function_name(&current_data->func->op_array TSRMLS_CC);
  
  if (func) {
    BEGIN_PROFILING(&hp_globals.entries, func, hp_profile_flag);
  }

  execute_internal(execute_data, return_value);

	if (func) {
    if (hp_globals.entries) {
      END_PROFILING(&hp_globals.entries, hp_profile_flag);
    }
    efree(func);
	}

}

/**
 * Proxy for zend_compile_file(). Used to profile PHP compilation time.
 *
 * @author kannan, hzhao
 */
ZEND_DLEXPORT zend_op_array* hp_compile_file(zend_file_handle *file_handle, int type TSRMLS_DC) {

  const char     *filename;
  char           *func;
  int             len;
  zend_op_array  *ret;
  int             hp_profile_flag = 1;


  filename = hp_get_base_filename(file_handle->filename);
  len      = strlen("load") + strlen(filename) + 3;
  func      = (char *)emalloc(len);
  snprintf(func, len, "load::%s", filename);

  BEGIN_PROFILING(&hp_globals.entries, func, hp_profile_flag);

  //php_printf("load::%s\n", filename);

  ret = _zend_compile_file(file_handle, type TSRMLS_CC);

  if (hp_globals.entries) {
    END_PROFILING(&hp_globals.entries, hp_profile_flag);
  }

  efree(func);
  return ret;
}

/**
 * Proxy for zend_compile_string(). Used to profile PHP eval compilation time.
 */
ZEND_DLEXPORT zend_op_array* hp_compile_string(zval *source_string, char *filename TSRMLS_DC) {
    char          *func;
    int            len;
    zend_op_array *ret;
    int            hp_profile_flag = 1;

    len  = strlen("eval") + strlen(filename) + 3;
    func = (char *)emalloc(len);
    snprintf(func, len, "eval::%s", filename);

    BEGIN_PROFILING(&hp_globals.entries, func, hp_profile_flag);
    ret = _zend_compile_string(source_string, filename TSRMLS_CC);
    if (hp_globals.entries) {
        END_PROFILING(&hp_globals.entries, hp_profile_flag);
    }

    efree(func);
    return ret;
}

/**
 * **************************
 * MAIN XHPROF CALLBACKS
 * **************************
 */

/**
 * This function gets called once when xhprof gets enabled.
 * It replaces all the functions like zend_execute, zend_execute_internal,
 * etc that needs to be instrumented with their corresponding proxies.
 */
static void hp_begin(long level, long xhprof_flags TSRMLS_DC) {
  if (!hp_globals.enabled) {
    int hp_profile_flag = 1;

    hp_globals.enabled      = 1;
    hp_globals.xhprof_flags = (uint32)xhprof_flags;
    
    /* Replace zend_compile with our proxy */
    _zend_compile_file = zend_compile_file;
    zend_compile_file  = hp_compile_file;
    
    /* Replace zend_compile_string with our proxy */
    _zend_compile_string = zend_compile_string;
    zend_compile_string = hp_compile_string;
  
    /* Replace zend_execute with our proxy */
    _zend_execute_ex = zend_execute_ex;
    zend_execute_ex  = hp_execute_ex;
    
    /* Replace zend_execute_internal with our proxy */
    _zend_execute_internal = zend_execute_internal;
    if (!(hp_globals.xhprof_flags & XHPROF_FLAGS_NO_BUILTINS)) {
      /* if NO_BUILTINS is not set (i.e. user wants to profile builtins),
       * then we intercept internal (builtin) function calls.
       */
      zend_execute_internal = hp_execute_internal;
    }
  
    

    /* Initialize with the dummy mode first Having these dummy callbacks saves
     * us from checking if any of the callbacks are NULL everywhere. */
    hp_globals.mode_cb.init_cb     = hp_mode_dummy_init_cb;
    hp_globals.mode_cb.exit_cb     = hp_mode_dummy_exit_cb;
    hp_globals.mode_cb.begin_fn_cb = hp_mode_dummy_beginfn_cb;
    hp_globals.mode_cb.end_fn_cb   = hp_mode_dummy_endfn_cb;


    /* Register the appropriate callback functions Override just a subset of
     * all the callbacks is OK. */
    switch(level) {
      case XHPROF_MODE_HIERARCHICAL:
        hp_globals.mode_cb.begin_fn_cb = hp_mode_hier_beginfn_cb;
        hp_globals.mode_cb.end_fn_cb   = hp_mode_hier_endfn_cb;
        break;
      case XHPROF_MODE_SAMPLED:
        hp_globals.mode_cb.init_cb     = hp_mode_sampled_init_cb;
        hp_globals.mode_cb.begin_fn_cb = hp_mode_sampled_beginfn_cb;
        hp_globals.mode_cb.end_fn_cb   = hp_mode_sampled_endfn_cb;
        break;
    }


    /* one time initializations */
    hp_init_profiler_state(level TSRMLS_CC);

    /* start profiling from fictitious main() */
    BEGIN_PROFILING(&hp_globals.entries, ROOT_SYMBOL, hp_profile_flag);
  }
}

/**
 * Called at request shutdown time. Cleans the profiler's global state.
 */
static void hp_end(TSRMLS_D) {
  /* Bail if not ever enabled */
  if (!hp_globals.ever_enabled) {
    return;
  }


  /* Stop profiler if enabled */
  if (hp_globals.enabled) {
    hp_stop(TSRMLS_C);
  }


  //return;

  /* Clean up state */
  hp_clean_profiler_state(TSRMLS_C);
}

/**
 * Called from xhprof_disable(). Removes all the proxies setup by
 * hp_begin() and restores the original values.
 */
static void hp_stop(TSRMLS_D) {
  int   hp_profile_flag = 1;

  /* End any unfinished calls */
  while (hp_globals.entries) {
    END_PROFILING(&hp_globals.entries, hp_profile_flag);
  }

 
  zend_execute_ex       = _zend_execute_ex;
  zend_execute_internal = _zend_execute_internal;
  zend_compile_file     = _zend_compile_file;
  zend_compile_string   = _zend_compile_string;

  /* Resore cpu affinity. */
  restore_cpu_affinity(&hp_globals.prev_mask);

  /* Stop profiling */
  hp_globals.enabled = 0;
}


/**
 * *****************************
 * XHPROF ZVAL UTILITY FUNCTIONS
 * *****************************
 */

/** Look in the PHP assoc array to find a key and return the zval associated
 *  with it.
 *
 *  @author mpal
 **/
static zval *hp_zval_at_key(char *key,zval *values) {
  zval *result = NULL;

  if (Z_TYPE_P(values) == IS_ARRAY) {

    HashTable *ht;
    zval     **value;
    uint     len = strlen(key) + 1;

    zend_string *keyz;
    keyz = strpprintf(0, key);


    ht = Z_ARRVAL_P(values);
    result = zend_hash_find(ht, keyz);
    
  } else {
    result = NULL;
  }

  return result;
}

/** Convert the PHP array of strings to an emalloced array of strings. Note,
 *  this method duplicates the string data in the PHP array.
 *
 *  @author mpal
 **/
static char **hp_strings_in_zval(zval  *values) {
  char   **result;
  size_t   count;
  size_t   ix = 0;

  if (!values) {
    return NULL;
  }

  if (Z_TYPE_P(values) == IS_ARRAY) {
    HashTable *ht;

    ht    = Z_ARRVAL_P(values);
    count = zend_hash_num_elements(ht);

    if((result = (char**)emalloc(sizeof(char*) * (count + 1))) == NULL) {
      return result;
    }

    for (zend_hash_internal_pointer_reset(ht);
         zend_hash_has_more_elements(ht) == SUCCESS;
         zend_hash_move_forward(ht)) {
      
      zend_string *str;
      zend_ulong  len;
      HashPosition idx;

      int    type;
      zval *data;

      type = zend_hash_get_current_key_ex(ht, &str, &len, &idx);
      /* Get the names stored in a standard array */
      if(type == HASH_KEY_IS_LONG) {
        data = zend_hash_get_current_data_ex(ht, &idx);
        if (Z_TYPE_P(data) == IS_STRING &&
            strcmp(Z_STRVAL_P(data), ROOT_SYMBOL)) { /* do not ignore "main" */
          result[ix] = estrdup(Z_STRVAL_P(data));
          ix++;
        }
      }
    }

  } else if(Z_TYPE_P(values) == IS_STRING) {
    if((result = (char**)emalloc(sizeof(char*) * 2)) == NULL) {
      return result;
    }
    result[0] = estrdup(Z_STRVAL_P(values));
    ix = 1;
  } else {
    result = NULL;
  }

  /* NULL terminate the array */
  if (result != NULL) {
    result[ix] = NULL;
  }

  return result;
}



///下面扩展相关
//////////////////////////
//////////////////////////

/* If you declare any globals in php_md_xhprof.h uncomment this:
ZEND_DECLARE_MODULE_GLOBALS(md_xhprof)
*/

/* True global resources - no need for thread safety here */
static int le_md_xhprof;

/* {{{ PHP_INI
 */

PHP_INI_BEGIN()

PHP_INI_ENTRY("md_xhprof.output_dir", "", PHP_INI_ALL, NULL)

PHP_INI_END()

/* }}} */


ZEND_BEGIN_ARG_INFO_EX(arginfo_xhprof_enable, 0, 0, 0)
  ZEND_ARG_INFO(0, flags)
  ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_xhprof_disable, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_xhprof_sample_enable, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_xhprof_sample_disable, 0)
ZEND_END_ARG_INFO()


/**
 * Start XHProf profiling in hierarchical mode.
 *
 * @param  long $flags  flags for hierarchical mode
 * @return void
 * @author kannan
 */
PHP_FUNCTION(xhprof_enable) {
  long  xhprof_flags = 0;                                    /* XHProf flags */
  zval *optional_array = NULL;         /* optional array arg: for future use */

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                            "|lz", &xhprof_flags, &optional_array) == FAILURE) {
    return;
  }

  hp_get_ignored_functions_from_arg(optional_array);

  hp_begin(XHPROF_MODE_HIERARCHICAL, xhprof_flags TSRMLS_CC);
  //php_printf("xhprof_enable\n");
}

/**
 * Stops XHProf from profiling in hierarchical mode anymore and returns the
 * profile info.
 *
 * @param  void
 * @return array  hash-array of XHProf's profile info
 * @author kannan, hzhao
 */
PHP_FUNCTION(xhprof_disable) {
	if (hp_globals.enabled) {
		hp_stop(TSRMLS_C);

    // php_printf("hp_globals in \n");
    

    // zval b;
    // zval c;

    // array_init(&c);

    // array_init(&b);
    // add_assoc_string(&b, "ke", "b");
    // add_assoc_zval(&b, "t", &c);
    // php_var_dump(&b, 0);

    // php_printf("c - nNumUsed:%d", Z_ARRVAL_P(&b)->nNumOfElements);

    // zval *d = &b;

    // zval *v;

    // zend_string *k = zend_string_init("t" ,strlen("t") ,0);
    // hp_print_zstr(k);
    // if(zend_hash_exists(Z_ARRVAL_P(d), k)){
    //   v = zend_hash_find(Z_ARRVAL_P(d), k);
    //   php_var_dump(v, 0);
    //   php_printf("zend_hash_exists:tt\n");
    // } else {
    //   php_printf("zend_hash_exists:ff\n");
    // }

		RETURN_ZVAL(&hp_globals.stats_count, 0, 1);
	}
  /* else null is returned */
}

/**
 * Start XHProf profiling in sampling mode.
 *
 * @return void
 * @author cjiang
 */
PHP_FUNCTION(xhprof_sample_enable) {
	long  xhprof_flags = 0;                                    /* XHProf flags */
  hp_get_ignored_functions_from_arg(NULL);
  hp_begin(XHPROF_MODE_SAMPLED, xhprof_flags TSRMLS_CC);
}

/**
 * Stops XHProf from profiling in sampling mode anymore and returns the profile
 * info.
 *
 * @param  void
 * @return array  hash-array of XHProf's profile info
 * @author cjiang
 */
PHP_FUNCTION(xhprof_sample_disable) {
  zend_string *strg;
  strg = strpprintf(0, "hello word");
  RETURN_STR(strg);
}




/* {{{ php_md_xhprof_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_md_xhprof_init_globals(zend_md_xhprof_globals *md_xhprof_globals)
{
	md_xhprof_globals->global_value = 0;
	md_xhprof_globals->global_string = NULL;
}
*/
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(md_xhprof)
{
    int i;

    REGISTER_INI_ENTRIES();
    hp_register_constants(INIT_FUNC_ARGS_PASSTHRU);

  	/* Get the number of available logical CPUs. */
    hp_globals.cpu_num = sysconf(_SC_NPROCESSORS_CONF);

  /* Get the cpu affinity mask. */
#ifndef __APPLE__
  	if (GET_AFFINITY(0, sizeof(cpu_set_t), &hp_globals.prev_mask) < 0) {
    	perror("getaffinity");
    	return FAILURE;
  	}
#else
  CPU_ZERO(&(hp_globals.prev_mask));
#endif

  	/* Initialize cpu_frequencies and cur_cpu_id. */
  	hp_globals.cpu_frequencies = NULL;
  	hp_globals.cur_cpu_id = 0;

    array_init(&(hp_globals.stats_count));

  	/* no free hp_entry_t structures to start with */
  	hp_globals.entry_free_list = NULL;

  	for (i = 0; i < 256; i++) {
    	hp_globals.func_hash_counters[i] = 0;
  	}

  	hp_ignored_functions_filter_clear();

#if defined(DEBUG)
    /* To make it random number generator repeatable to ease testing. */
    srand(0);
#endif

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(md_xhprof)
{
	/* Make sure cpu_frequencies is free'ed. */
  clear_frequencies();

  /* free any remaining items in the free list */
	hp_free_the_free_list();

	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(md_xhprof)
{
#if defined(COMPILE_DL_MD_XHPROF) && defined(ZTS)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(md_xhprof)
{
	hp_end(TSRMLS_C);
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(md_xhprof)
{
  char buf[SCRATCH_BUF_LEN];
  char tmp[SCRATCH_BUF_LEN];
  int i;
  int len;

	php_info_print_table_start();
	php_info_print_table_header(2, "md_xhprof support", "enabled");

  len = snprintf(buf, SCRATCH_BUF_LEN, "%d", hp_globals.cpu_num);
  buf[len] = 0;
  php_info_print_table_header(2, "CPU num", buf);

  if (hp_globals.cpu_frequencies) {
    
    /* Print available cpu frequencies here. */
    php_info_print_table_header(2, "CPU logical id", " Clock Rate (MHz) ");

    for (i = 0; i < hp_globals.cpu_num; ++i) {
      len = snprintf(buf, SCRATCH_BUF_LEN, " CPU %d ", i);
      buf[len] = 0;
      len = snprintf(tmp, SCRATCH_BUF_LEN, "%f", hp_globals.cpu_frequencies[i]);
      tmp[len] = 0;
      php_info_print_table_row(2, buf, tmp);
    }
  }

  php_info_print_table_row(2, "Version", XHPROF_VERSION);
	php_info_print_table_row(2, "PHP_VERSION_ID", PHP_VERSION);

	php_info_print_table_end();

}
/* }}} */

/* {{{ md_xhprof_functions[]
 *
 * Every user visible function must have an entry in md_xhprof_functions[].
 */
const zend_function_entry md_xhprof_functions[] = {
    PHP_FE(xhprof_enable, arginfo_xhprof_enable)
    PHP_FE(xhprof_disable, arginfo_xhprof_disable)
    PHP_FE(xhprof_sample_enable, arginfo_xhprof_sample_enable)
  	PHP_FE(xhprof_sample_disable, arginfo_xhprof_sample_disable)
	PHP_FE_END	/* Must be the last line in md_xhprof_functions[] */
};
/* }}} */

/* {{{ md_xhprof_module_entry
 */
zend_module_entry md_xhprof_module_entry = {
	STANDARD_MODULE_HEADER,
	"md_xhprof",
	md_xhprof_functions,
	PHP_MINIT(md_xhprof),
	PHP_MSHUTDOWN(md_xhprof),
	PHP_RINIT(md_xhprof),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(md_xhprof),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(md_xhprof),
	PHP_MD_XHPROF_VERSION,
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_MD_XHPROF
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(md_xhprof)
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
