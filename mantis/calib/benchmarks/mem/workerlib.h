/*
 * Copyright 2003,2004 Justin Moore, justin@cs.duke.edu
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef GAMUT_WORKERLIB_H
#define GAMUT_WORKERLIB_H

#include <stdio.h>
#include "workeropts.h"

typedef void* (*gamut_worker)(void *opts);

/*
 * Insert a worker thread, but don't actually launch it yet.
 */ 
extern int insert_worker(gamut_opts *opts, worker_class wcls, char *attrs);

/*
 * Start a queued worker thread.
 */
extern int start_queued_worker(gamut_opts *opts, worker_class wcls, int windex);

/*
 * Try to find a worker thread with the given
 *   worker ID, label, or thread ID.
 */
extern int find_worker_by_wid(gamut_opts *opts, worker_class *wlcs,
                              workerID wid, int *widx);
extern int find_worker_by_label(gamut_opts *gopts, worker_class *wcls,
                                char *wlabel, int *widx);
extern int find_worker_by_tid(gamut_opts *gopts, worker_class *wcls,
                              pthread_t tid, int *widx);

/*
 * Try to find a worker with the given 'after' tag.
 *   If wcls is valid on entry, search only in that class.
 *   Otherwise, search in all classes and fill in wcls with the
 *   class of where we found it (if at all).
 */
extern int find_after_by_label(gamut_opts *gopts, worker_class *wcls,
                               char *wlabel, int *widx);

/*
 * Kill off an existing worker thread.
 */
extern int kill_worker(gamut_opts *opts, worker_class wcls, int windex);

/*
 * Kill all existing threads.
 */
extern void killall_workers(gamut_opts *opts);

/*
 * When each worker thread starts up, it registers itself with the master
 *   thread.  Once all threads have started up, the master thread sets all
 *   workers off and running.
 */
extern int worker_register(gamut_opts *opts, worker_class wcls);

/*
 * Un-register a thread so we the main thread can prepare the slot
 *   for its next occupant.
 */
extern void worker_unregister(gamut_opts *opts, worker_class wcls);

#endif /* GAMUT_WORKERCTL_H */
