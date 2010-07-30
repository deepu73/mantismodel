/*
 * Copyright 2005 Justin Moore, justin@cs.duke.edu
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef GAMUT_CONSTANTS_H
#define GAMUT_CONSTANTS_H

/*
 * Number of ms and us per second
 */
#define MS_SEC 1000LL
#define US_SEC 1000000LL

/*
 * What's the maximum number of workers we can have
 *   for each of the worker classes?
 */
#define MAX_CPUS   32
#define MAX_MEMS   32
#define MAX_DIOS   32
#define MAX_NIOS   32

#define MAX_WQUEUE  16  /* Queue length for keeping track of workers */
#define MAX_LINKLEN 16  /* Maximum number of workers per link */
#define MAX_LINKS   16  /* Maximum number of worker sets */
#define MAX_AFTERS  8   /* Number of other workers we can follow */

/*
 * How many worker epochs per second?
 *   Default is 20, meaning an epoch lasts 50ms.
 */
#define WORKER_EPOCHS_PER_SEC 20
#define US_PER_WORKER_EPOCH   (US_SEC / WORKER_EPOCHS_PER_SEC)

#define DEF_BMARK_TRIALS 10 /* num of benchmark trials for '-b' */

#define LISTEN_BACKLOG 5    /* Backlog size for TCP connections */
#define CONN_WAIT      3    /* wait 3 seconds for a TCP connection */
#define MAX_RECV_TRIES 5    /* # of times we try to get UDP data */

/*
 * Modes for disk worker file creation.
 */
#define C_RDONLY    0  /* Only read this file */
#define C_IFNEXIST  1  /* Only create/modify if file doesn't exist */
#define C_OVERWRITE 2  /* Overwrite or create file at will */

/*
 * Array indices for disk and network I/O bookkeeping.
 */
#define C_IOREAD    0  /* Array index for read statistics */
#define C_IOWRITE   1  /* Array index for write statistics */
#define C_IOSEEK    2  /* Array index for seek statistics */

/*
 * Used for cleaning worker options and copying worker options.
 *   Do we keep or not keep identifying information (label, run-time
 *   parameters, etc) for the target or destination worker?
 */
#define WC_NOKEEPID 0  /* Don't keep worker identifying information */
#define WC_KEEPID   1  /* Keep worker identifying information */

/*
 * Establish a global locking order
 */
#define MASTER_LOCK_IDX    1
#define AFTER_LOCK_IDX     (MASTER_LOCK_IDX + 1)
#define REAPER_LOCK_IDX    (AFTER_LOCK_IDX + 1)
#define WAITING_LOCK_IDX   (REAPER_LOCK_IDX + 1) 
#define STATS_LOCK_IDX     (WAITING_LOCK_IDX + 1)
#define LINK_LOCK_IDX      (STATS_LOCK_IDX + 1)
#define INPUT_LOCK_IDX     (LINK_LOCK_IDX + 1)
 
#define CPU_CLASS_LOCK_IDX (INPUT_LOCK_IDX + 1)
#define MEM_CLASS_LOCK_IDX (CPU_CLASS_LOCK_IDX + 1)
#define DIO_CLASS_LOCK_IDX (MEM_CLASS_LOCK_IDX + 1)
#define NIO_CLASS_LOCK_IDX (DIO_CLASS_LOCK_IDX + 1)

#define CPU_BASE_LOCK_IDX  (NIO_CLASS_LOCK_IDX + 1)
#define MEM_BASE_LOCK_IDX  (CPU_BASE_LOCK_IDX + MAX_CPUS)
#define DIO_BASE_LOCK_IDX  (MEM_BASE_LOCK_IDX + MAX_MEMS)
#define NIO_BASE_LOCK_IDX  (DIO_BASE_LOCK_IDX + MAX_DIOS)

#define MAX_LOCK_IDX       (NIO_BASE_LOCK_IDX + MAX_NIOS)

/*
 * Are we adding or deleting a lock?
 *   Used for debugging synchronization problems.
 */
#define L_ADD 0
#define L_DEL 1

#endif /* GAMUT_CONSTANTS_H */
