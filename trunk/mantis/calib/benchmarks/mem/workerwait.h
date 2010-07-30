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

#ifndef GAMUT_WORKERWAIT_H
#define GAMUT_WORKERWAIT_H

/*
 * Tag all threads that will exit on their own
 *   (i.e., have a time limit or a maximum amount of work)
 *   as having the master wait for them.
 */
extern int tag_worker_mwait(gamut_opts *gopts, worker_class wcls);

#endif /* GAMUT_WORKERWAIT_H */
