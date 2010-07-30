/*
 * Copyright 2003-2005 Justin Moore, justin@cs.duke.edu
 * Copyright 2004 HP Labs, justinm@hpl.hp.com
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef GAMUT_WORKEROPTS_H
#define GAMUT_WORKEROPTS_H

#include <netdb.h>     /* for uint{16,32}_t        */
#include <pthread.h>   /* for the pthread_t struct */
#include <sys/time.h>  /* For struct timeval       */

#include "constants.h" /* for several #define's    */
#include "utilio.h"    /* for SMBUFSIZE              */

/********************** Begin option data structures ******************/

typedef uint32_t workerID;

#define WID_CLS_ALL (workerID)0  /* Worker ID for an entire class  */

/*
 * Types of commands we can send to the master thread.
 */
typedef enum {
  MCMD_FREE = 0, /* The master is free to accept commands      */
  MCMD_INPUT,    /* Command from the worker parsing commands   */
  MCMD_AFTER,    /* Start a worker because of an 'after' param */
  MCMD_EXIT,     /* The master thread should exit and clean up */
  MCMD_LAST,     /* Token value for bounds checking            */
  MCMD_ERROR
} master_cmd;

#define is_valid_mcmd(m) ((m >= 0) && (m < MCMD_LAST))

/*
 * Classes of workers, plus some bounds.
 */
typedef enum {
  CLS_CPU = 0,
  CLS_MEM,
  CLS_DISK,
  CLS_NET,
  CLS_LAST,
  CLS_ALL,
  CLS_NONE,
  CLG_ERROR
} worker_class;

#define is_valid_cls(c) ((c >= 0) && (c < CLS_LAST))

/******************************************************************/
/******************************************************************/

/*
 * Generic worker class/worker index pairing.
 */
typedef struct {
  worker_class wcls;
  int worker_index;
} worker_data;

/*
 * Generic synchronized counter.  Useful for having one worker
 *   wait for other workers to perform some action.
 */
typedef struct {
  pthread_mutex_t c_lock;
  pthread_cond_t  c_cond;
  uint32_t        count;
} sync_counter;

/*
 * Generic synchronization for a worker.
 */
typedef struct {
  pthread_t       tid;       /* Thread ID */
  pthread_mutex_t lock;      /* Lock for thread */
  pthread_cond_t  cond;      /* Condition variable (used for exiting) */

  uint32_t        lock_order[MAX_LOCK_IDX]; /* Used in lock debugging */
  uint32_t        curr_lock; /* Current position in lock_order */
} thread_sync;

/*
 * A slightly fancier version of the sync counter.
 *   This also includes a list of workers on which to operate
 *   and some synchronization variables to let us know when
 *   to operate on them.
 */
typedef struct {
  thread_sync      t_sync;

  worker_data      wdata[MAX_WQUEUE];
  uint32_t         wqueue_size;
  volatile uint8_t exiting;
} worker_sync;

/*
 * Keep track of the workers that are linked together.
 *   This data structure represents one set of linked workers.
 */
typedef struct {
  char            label[SMBUFSIZE];   /* Name of this link */
  worker_data     wdata[MAX_LINKLEN]; /* Class and index of workers */
  uint32_t        num_linked;         /* Num. of workers in the link */
} worker_link;

/*
 * High-level data structure for all links.
 */
typedef struct {
  pthread_mutex_t link_lock;        /* Lock for all links */
  worker_link     wlink[MAX_LINKS]; /* Set of worker links */
} worker_links;

/*
 * This allows other workers to send commands to the main thread.
 */
typedef struct {
  thread_sync     t_sync;        /* Info about the master thread */
  pthread_mutex_t start_lock;    /* Used to synchronize worker starts */

  master_cmd      mcmd;          /* Type of command we were given */
  char            mbuf[BUFSIZE]; /* Command buffer (for options) */
} master_ctl;

/*
 * Keep statistics on what's happened.
 */
typedef struct {
  pthread_mutex_t stats_lock;
  int32_t workers_parsed;     /* Number of parse_worker_opts calls */
  int32_t workers_invalid;    /* Number of invalid worker requests */
  int32_t workers_spawned;    /* How many pthread_create calls     */
  int32_t workers_pending;    /* Pending and not started           */
  int32_t workers_waiting;    /* Number waiting on other workers   */
  int32_t workers_linked;     /* Number operating in links         */
  int32_t workers_leading;    /* Number 'leading' other workers    */
  int32_t workers_running;    /* Workers currently running         */
  int32_t workers_linkwait;   /* Workers waiting for the prev link */
  int32_t workers_moved;      /* How many migrated (unused ATM)    */
  int32_t workers_exiting;    /* Num. that have 'exiting' flagged  */
  int32_t workers_reaped;     /* How many the reaper has collected */
} worker_stats;

/******************************************************************/
/*         These parameters are shared by all workers.            */
/******************************************************************/
typedef struct {
  thread_sync    t_sync;        /* Synchronization for this worker */
  worker_class   wcls;          /* What kind of worker are we? */
  uint32_t       widx;          /* Our index in the worker array */
  uint32_t       lockpos;       /* Position in global locking order */

  workerID       wid;           /* Worker ID */
  struct timeval start_time;    /* When did this worker start? */
  struct timeval mod_time;      /* When were worker values updated? */

  uint64_t  missed_deadlines;   /* Number of epochs we were late */
  uint64_t  missed_usecs;       /* Total usecs of missed deadlines */
  uint64_t  total_deadlines;    /* Total number of epochs we've run */

  char      label[SMBUFSIZE];   /* Name of the worker */
  char      after[MAX_AFTERS][SMBUFSIZE]; /* Workers we're waiting on */
  uint32_t  num_afters;         /* Number of workers we're waiting on */
  uint32_t  exec_time;          /* Time, in seconds, of execution */
  uint64_t  max_work;           /* Total operations to perform */

  uint64_t  link_work;          /* Amount of work to do in our link */
  void     *prev_worker;        /* Previous link worker (shared_opts) */
  void     *next_worker;        /* Next worker in link (shared_opts) */

  volatile uint16_t used:1;     /* This slot is taken */
  volatile uint16_t pending:1;  /* Worker is queued but not running */
  volatile uint16_t waiting:1;  /* Worker is waiting on other worker */
  volatile uint16_t linked:1;   /* Are we linked to other workers? */
  volatile uint16_t leading:1;  /* Someone comes after this worker */
  volatile uint16_t running:1;  /* Worker is away and running */
  volatile uint16_t linkwait:1; /* Waiting for previous linked worker */
  volatile uint16_t dirty:1;    /* Worker needs to reload params */
  volatile uint16_t mwait:1;    /* Master is waiting on worker */
  volatile uint16_t exiting:1;  /* Worker needs to shut down */

  /* These fields will be used in the future */
  volatile uint16_t msource:1;  /* Worker is source of a migration */
  volatile uint16_t mdest:1;    /* Worker is a migration destination */
  volatile uint16_t paused:1;   /* Worker is paused for the moment */

  volatile uint16_t padding:3;  /* Pad out the 16-bit value */
} shared_opts;

/*
 * Number of these options that can be specified in a 'wctl' command.
 */
#define NUM_SHD_OPTS 4

/******************************************************************/
/******************************************************************/

/*
 * Enable modular CPU utilization functions.
 */
typedef struct {
  uint8_t  count8;
  uint16_t count16;
  uint32_t count32;
  uint64_t count64;

  float    count_f;
  double   count_d;
} cpu_burn_opts;

/*
 * Handy definition for zeroing out a cpu_burn_opts struct.
 */
#define ZERO_CPU_OPTS { 0, 0, 0, 0, 0.0, 0.0 }

/*
 * The first pointer will be to a cpu_opts struct, but we define
 *   it to be void* to prevent circular declarations.
 */
typedef void (*cpu_burn_func)(void *cpu, cpu_burn_opts *cbopts);
typedef void (*cpu_opts_func)(void *cpu, cpu_burn_opts *srcopts,
                              cpu_burn_opts *dstopts);

typedef struct {
  shared_opts   shopts;      /* Shared options */

  uint32_t      percent_cpu; /* Percent CPU used */
  cpu_burn_func cbfunc;      /* Function we call to use CPU */

  uint64_t      total_work;  /* Total count of this CPU worker */
} cpu_opts;

/*
 * Number of CPU options that can be specified in a 'wctl' command.
 */
#define NUM_CPU_OPTS (2 + NUM_SHD_OPTS)

/******************************************************************/
/******************************************************************/

typedef struct {
  shared_opts shopts;   /* Shared options */

  uint64_t total_ram;   /* Total RAM allocated */
  uint64_t working_ram; /* Size of the working set */
  uint64_t blksize;     /* Size of a block (defaults to 1 page) */
  uint64_t iorate;      /* Rate to touch memory */
  uint32_t stride;      /* Number of sequential blks per random blk */

  /*
   * These last two are not set by the user, but instead calculated.
   */
  uint64_t ntblks;      /* Total number of blocks we'll allocate */
  uint64_t nwblks;      /* Number of blocks in the working set */

  uint64_t total_memio; /* Total amount of memory I/O performed */
} mem_opts;

#define NUM_MEM_OPTS (5 + NUM_SHD_OPTS)

/******************************************************************/
/******************************************************************/

typedef struct {
  shared_opts shopts;   /* Shared options */

  char *file;           /* File name */
  uint32_t blksize;     /* Blocksize */
  uint32_t nblks;       /* Total number of blocks in the file */
  uint16_t create;      /* Create the file? */
  uint32_t iorate;      /* I/O rate */
  uint32_t sync_f;      /* How often to sync buffers? */
  struct {              /* START I/O ratio statistics */
    uint16_t numrds;    /* Number of reads */
    uint16_t numwrs;    /* Number of writes */
    uint16_t numsks;    /* Number of seeks */
  } iomix;              /* END I/O ratio statistics */

  int64_t total_diskio;  /* Total amount of work done so far */
  int64_t num_diskio[3]; /* Number of disk I/Os by category */
  int64_t io_usec[3];    /* Usecs per each category of I/O */
} dio_opts;

#define NUM_DIO_OPTS (8 + NUM_SHD_OPTS)

/******************************************************************/
/******************************************************************/

typedef struct {
  shared_opts shopts;     /* Shared options */

  uint32_t addr;          /* Remote end address in network-byte-order */
  uint16_t port;          /* Remote port */
  uint16_t mode;          /* Mode (read/write) */
  int32_t  protocol;      /* Protocol number (a la getprotobyname) */
  uint32_t pktsize;       /* Packet size */
  uint64_t iorate;        /* I/O rate */

  int64_t total_netio;    /* Total amount of work remaining */
  int64_t netio_bytes[2]; /* Number of I/O bytes (read and write) */
  int64_t io_usec[2];     /* How long did it take to do the I/O? */
} nio_opts;

#define NUM_NIO_OPTS (6 + NUM_SHD_OPTS)

/******************************************************************/
/******************************************************************/

typedef struct {
  master_ctl   mctl;      /* Master thread (needed for signalling)   */
  worker_stats wstats;    /* Statistics for current and past workers */
  worker_sync  r_sync;    /* List of workers waiting for the reaper  */
  sync_counter wcounter;  /* Number of workers we're waiting on      */
  worker_sync  a_sync;    /* List of workers waiting, post-'after'   */
  worker_sync  i_sync;    /* Synchronization for the input thread    */
  worker_links wlinks;    /* Set of linked workers */
 
  cpu_opts cpu[MAX_CPUS];
  pthread_mutex_t cpu_lock;
    
  mem_opts mem[MAX_MEMS];
  pthread_mutex_t mem_lock;
  
  dio_opts disk_io[MAX_DIOS];
  pthread_mutex_t dio_lock;
  
  nio_opts net_io[MAX_NIOS];
  pthread_mutex_t nio_lock;
} gamut_opts;

/*********************** End option data structures *******************/

/*********************** Begin function declarations ******************/

/*
 * Initialize all the portions of the options struct.
 *   Mainly this involves the threading variables.
 *   Exit on failure.
 */
extern void init_opts(gamut_opts *gopts);

/*
 * Parse the class-specific options and fill in the data structures.
 */
extern int parse_worker_opts(gamut_opts *gopts, worker_class wcls,
                             int widx, char *attrs);

/*
 * Validate the options that have been passed to the structure.
 */
extern int validate_worker_opts(gamut_opts *gopts,
                                worker_class wcls, int widx);

/*
 * Copy the contents of one worker to another.  Let us know if
 *   we should keep the identity of the original worker?
 */
extern void copy_worker_opts(gamut_opts *gopts, worker_class wcls,
                             int srcwidx, int destwidx, int keepID);

/*
 * Clean out a struct to prepare it for use (or re-use).
 *   This is done IN PLACE OF a memet(struct, 0, sizeof(struct))
 *   Do we keep the identity of the worker (tid, wid, headers, label)?
 */
extern void clean_worker_opts(gamut_opts *gopts, worker_class wcls,
                              int widx, int keepID);

/*
 * Get the "shared opts" struct of a given worker.
 */
extern shared_opts* get_shared_opts(gamut_opts *gopts,
                                    worker_class wcls, int widx);

/*********************** End function declarations ********************/

#endif /* GAMUT_WORKEROPTS_H */
