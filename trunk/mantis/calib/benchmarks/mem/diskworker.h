/*
 * Copyright 2003,2004 Justin Moore, justin@cs.duke.edu
 * Copyright 2004 HP Labs, justinm@hpl.hp.com
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef GAMUT_DISKWORKER_H
#define GAMUT_DISKWORKER_H

#include <netdb.h>

#define MAX_DISK_SEEKS 100

/*
 * Used so we can generate a random number and select an I/O
 *   operation based on that number.
 */
typedef struct {
  struct {
    int32_t min;
    int32_t max;
  } reads;
  struct {
    int32_t min;
    int32_t max;
  } writes;
  struct {
    int32_t min;
    int32_t max;
  } seeks;
  int32_t maxval;
} iorange;

/*
 * Fire off an I/O thread to do a certain number of I/Os per second.
 */
extern void* diskworker(void *opts);

#endif /* GAMUT_DISKWORKER_H */
