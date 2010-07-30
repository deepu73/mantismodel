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

#ifndef GAMUT_WORKERINFO_H
#define GAMUT_WORKERINFO_H

#include "workeropts.h"

/*
 * Print the contents of the statistics header.
 */
extern void print_stats_info(gamut_opts *gopts, int detail);

/*
 * Print all the information for a given worker.
 */
extern void print_worker_info(gamut_opts *gopts, worker_class wcls,
                              int widx, int detail);

#endif /* GAMUT_WORKERINFO_H */
