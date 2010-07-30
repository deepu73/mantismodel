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

#ifndef GAMUT_WORKERSYNC_H
#define GAMUT_WORKERSYNC_H

#include <pthread.h>

#include "workeropts.h"

/************************* Begin data structures **********************/
typedef struct {
  worker_data wdata[MAX_LOCK_IDX];
  int32_t     num_locks;
} worker_order;
/************************** End data structures ***********************/

/********************** Begin function declarations *******************/

/*
 * Lock/unlock the master lock, wait for a signal,
 *   or signal the master.
 */
extern int lock_master(gamut_opts *gopts);
extern int unlock_master(gamut_opts *gopts);
extern int wait_master(gamut_opts *gopts);
extern int signal_master(gamut_opts *gopts);
extern int broadcast_master(gamut_opts *gopts);

/*
 * Lock/unlock the 'after' lock, wait for a signal,
 *   or signal the 'after' thread.
 */
extern int lock_after(gamut_opts *gopts);
extern int unlock_after(gamut_opts *gopts);
extern int wait_after(gamut_opts *gopts);
extern int signal_after(gamut_opts *gopts);

/*
 * Lock/unlock the reaper lock, wait for a signal,
 *   or signal the reaper.
 */
extern int lock_reaper(gamut_opts *gopts);
extern int unlock_reaper(gamut_opts *gopts);
extern int wait_reaper(gamut_opts *gopts);
extern int signal_reaper(gamut_opts *gopts);

/*
 * Lock/unlock the waiting lock, wait for a signal,
 *   or signal the waiting thread.
 */
extern int lock_waiting(gamut_opts *gopts);
extern int unlock_waiting(gamut_opts *gopts);
extern int wait_waiting(gamut_opts *gopts, uint64_t wait_usec);
extern int signal_waiting(gamut_opts *gopts);

/*
 * Lock/unlock the statistics for modifications.
 */
extern int lock_stats(gamut_opts *gopts);
extern int unlock_stats(gamut_opts *gopts);

/*
 * Lock/unlock the input thread.
 */
extern int lock_input(gamut_opts *gopts);
extern int unlock_input(gamut_opts *gopts);

/*
 * Lock/unlock the worker link lock.
 */
extern int lock_link(gamut_opts *gopts);
extern int unlock_link(gamut_opts *gopts);

/*
 * Lock/unlock the worker start lock.
 */
extern int lock_start(gamut_opts *gopts);
extern int unlock_start(gamut_opts *gopts);

/*
 * Lock/unlock a worker class in preparation for modifications.
 */
extern int lock_class(gamut_opts *gopts, worker_class wcls);
extern int unlock_class(gamut_opts *gopts, worker_class wcls);

/*
 * Lock/unlock a worker struct in preparation for modifications.
 */
extern int lock_worker(gamut_opts *gopts, worker_class wcls, int widx);
extern int unlock_worker(gamut_opts *gopts, worker_class wcls, int widx);
extern int wait_worker(gamut_opts *gopts, worker_class wcls, int widx);
extern int signal_worker(gamut_opts *gopts, worker_class wcls, int widx);

/*
 * Since a worker might have to lock other workers -- i.e., during
 *   thread exit, linked operations, etc -- we'll use this to make
 *   ordered locking easier.
 */
extern int init_worker_order(worker_order *worder);
extern int append_worker(worker_order *worder, worker_class wcls,
                         uint32_t widx);
extern int lock_worker_order(gamut_opts *gopts, worker_order *worder);
extern int unlock_worker_order(gamut_opts *gopts, worker_order *worder);

/*
 * Dump lock information to a buffer.
 */
extern int get_lock_info(char *buf, uint32_t bufsize, thread_sync *t_sync);

/*********************** End function declarations ********************/

#endif /* GAMUT_WORKERSYNC_H */
