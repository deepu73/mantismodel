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

#ifndef GAMUT_MAIN_CTL_H
#define GAMUT_MAIN_CTL_H

#include <stdio.h>

#include "workeropts.h"

/*
 * Link commands
 */
typedef enum {
  LINK_QUEUE = 0,
  LINK_START,
  LINK_DEL,
  LINK_LAST,
  LINK_ERROR
} link_cmd;

#define is_valid_lcmd(c) ((c >= 0) && (c < LINK_LAST))

/*
 * Worker control (wctl) commands.
 */
typedef enum {
  WCTL_ADD = 0,
  WCTL_QUEUE,
  WCTL_START,
  WCTL_MOD,
  WCTL_DEL,
  WCTL_LAST,
  WCTL_ERROR
} worker_cmd;

#define is_valid_wcmd(c) ((c >= 0) && (c < WCTL_LAST))

/*
 * The Real Deal.
 *
 * Wait on the lock and accept commands from other threads
 *   that poke at us.
 */
extern void execute_gamut(gamut_opts *opts);

/*
 * This is how we notify the master thread that we need it
 *   to do something.
 */
extern int send_master_cmd(gamut_opts *gopts, worker_cmd wcmd,
                           char *cmdstr);

#endif /* GAMUT_MAIN_CTL_H */
