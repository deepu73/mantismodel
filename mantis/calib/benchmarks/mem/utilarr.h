/* 
 * Copyright 2001-2004 Justin Moore, justin@cs.duke.edu
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
 * Dynamically growable data array.
 *
 * Keeps track of the size of the objects in the array,
 *   the number of currently allocated slots, and the
 *   number of currently used slots.
 *
 * It is up to the programmer to correctly update the
 *   currUsed field so the TestAnd*Array functions
 *   will work correctly.
 *
 * It is also up to the programmer to keep the array
 *   in sorted (or unsorted) order.
 */

#ifndef _UTIL_ARRAY_H
#define _UTIL_ARRAY_H

#include <netdb.h>

typedef struct {
  void *dat;
  int is_sorted;
  uint32_t currUsed;
  uint32_t currAlloc;
  uint32_t objSize;
} growArray;

extern int InitGrowArray(growArray **arr, uint32_t numObj, uint32_t objSize,
  int is_sorted);
extern int TestAndGrowArray(growArray *arr, uint32_t numAdded);
extern int TestAndShrinkArray(growArray *arr);
extern int DelGrowArray(growArray **arr);

#endif /* _UTIL_ARRAY_H */
