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

#ifndef GAMUT_MEMWORKER_H
#define GAMUT_MEMWORKER_H

/*
 * Allocate memory of a given size, then cycle through the working
 *   set size, touching each page to force it back into memory.
 */
extern void* memworker(void *opts);

#endif /* GAMUT_MEMWORKER_H */
