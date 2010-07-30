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

#ifndef GAMUT_LINKCTL_H
#define GAMUT_LINKCTL_H

#include "workeropts.h"

/*
 * Simple commands to queue, start, or delete linked workers.
 */
extern int queue_link(gamut_opts *opts, char *llabel, char *attrs);
extern int start_link(gamut_opts *opts, char *llabel);
extern int del_link(gamut_opts *opts, char *llabel);

#endif /* GAMUT_LINKCTL_H */
