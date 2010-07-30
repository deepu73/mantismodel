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

#ifndef GAMUT_LINKLIB_H
#define GAMUT_LINKLIB_H

#include "workeropts.h"

/*
 * Insert a link, but don't actually start it yet.
 */
extern int insert_link(gamut_opts *gopts, char *llabel, char *attrs);

/*
 * Start a set of linked workers.
 */
extern int start_queued_link(gamut_opts *gopts, int lidx);

/*
 * Have a linked worker wait for the go-ahead to start.
 */
extern int link_start_wait(gamut_opts *gopts, worker_class wcls,
                           int widx);

/*
 * If we're linked to one or more other workers,
 *   this function will wait until it's our turn to do work
 *   or it's time for us to exit.
 *
 * This function returns -1 on error,
 *                        0 if we need to exit, or
 *                        1 if it's our turn.
 */
extern int link_next_wait(gamut_opts *gopts, worker_class wcls,
                          int widx, double epochs_per_link,
                          double *curr_epochs, int32_t *target_epochs);

/*
 * Remove ourselves from the link of workers.
 */
extern int link_remove(gamut_opts *gopts, worker_class wcls, int widx);

/*
 * Find a link by the given label or member worker.
 */
extern int find_link_by_label(gamut_opts *gopts, char *llabel,
                              int *lidx);
extern int find_link_by_worker(gamut_opts *gopts, worker_class wcls,
                               int widx, int *lidx);

/*
 * Kill a set of linked workers.
 */
extern int kill_link(gamut_opts *gopts, int lidx);

#endif /* GAMUT_LINKLIB_H */
