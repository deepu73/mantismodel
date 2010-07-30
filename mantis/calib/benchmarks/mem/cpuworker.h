/*
 * Copyright 2003-2005 Justin Moore, justin@cs.duke.edu
 * Copyright 2004 HP Labs, justinm@hpl.hp.com
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef GAMUT_CPUWORKER_H
#define GAMUT_CPUWORKER_H

/*
 * Burn CPU at a steady rate in this thread.
 */
extern void* cpuworker(void *opts);

#endif /* GAMUT_CPUWORKER_H */
