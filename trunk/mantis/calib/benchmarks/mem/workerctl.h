/*
 * Copyright 2003-2005 Justin Moore, justin@cs.duke.edu
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef GAMUT_WORKERCTL_H
#define GAMUT_WORKERCTL_H

#include "workeropts.h"

/*
 * Simple commands to add, modify, or delete a workload thread.
 */
extern int add_worker(gamut_opts *opts, worker_class wcls, char *attrs);
extern int queue_worker(gamut_opts *opts, worker_class wcls, char *attrs);
extern int start_worker(gamut_opts *opts, worker_class wcls, char *wlabel);
extern int mod_worker(gamut_opts *opts, worker_class wcls,
                      char *wlabel, char *newattrs);
extern int del_worker(gamut_opts *opts, worker_class wcls, char *wlabel);

/*
 * Check to see if there are pending threads we can start.
 */
extern int chk_worker(gamut_opts *opts);

#endif /* GAMUT_WORKERCTL_H */
