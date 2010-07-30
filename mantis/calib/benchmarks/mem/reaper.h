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

#ifndef GAMUT_REAPER_H
#define GAMUT_REAPER_H

#include "workeropts.h"

/*
 * Fire up the reaper thread.
 */
extern void start_reaper(gamut_opts *gopts);

/*
 * Shut down the reaper thread.
 *   All other threads must be killed off for this to be OK.
 */
extern void stop_reaper(gamut_opts *gopts);

#endif /* GAMUT_REAPER_H */
