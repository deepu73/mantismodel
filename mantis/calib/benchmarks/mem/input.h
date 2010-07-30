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

#ifndef GAMUT_INPUT_H
#define GAMUT_INPUT_H

#include "workeropts.h"

/*
 * Definition of commands that gamut will execute
 *   in response to typed commands.
 */
typedef int (*gamut_handler)(gamut_opts *gopts, char *cmdstr);

/*
 * We keep track of all the commands that will execute
 *   on the front-end as well as the back-end.  We keep tabs
 *   on which to execute locally (within the input thread)
 *   and which to execute remotely (on the master).
 *
 * Command handlers with a NULL gamut_handler are executed
 *   inside the master.
 */
typedef struct {
  char            *cmd;       /* The name of the command */
  gamut_handler func;       /* The function to execute */
} cmd_handler;

/*
 * Fire up the input thread.
 */
extern void start_input(gamut_opts *gopts);

/*
 * High-level API to input parsing code.
 */
extern void parse_input(gamut_opts *gopts, char *infname,
                        uint8_t timed);

/*
 * Shut down the input thread.
 */
extern void stop_input(gamut_opts *gopts);

#endif /* GAMUT_INPUT_H */
