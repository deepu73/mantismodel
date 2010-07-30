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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utilarr.h"

/*
 * Give us a pointer to a grow array, and we allocate
 * an initial set of (empty) data for that array.
 * Set internal variables so TestAndGrowArray can
 * work properly.
 *
 * Return -1 on error,
 *         0 on successful allocation,
 *         1 if the array has already been allocated.
 */
int InitGrowArray(growArray **arr, uint32_t numObj, uint32_t objSize,
  int is_sorted)
{
  growArray *tmpArr;

  /* Bad parameters passed to us. */
  if(!arr || !objSize)
    return -1;

  /*
   * If we were given a pointer to a NULL pointer,
   * allocate the initial growArray data structure.
   */
  if(!(*arr)) {
    *arr = (growArray *)calloc(1, sizeof(growArray));
    if(!(*arr))
      return -1;
  }
  tmpArr = *arr;

  /*
   * In case functions that insert similar data into growArrays
   *   need to know whether or not to sort data as it comes in.
   */
  tmpArr->is_sorted = is_sorted;

  /*
   * It is OK to have a 0-sized array to start out with,
   *   just as long as we know the object size.  If we
   *   know the object size we can reallocate as the
   *   space is needed.
   */
  if(!numObj) {
    tmpArr->dat = NULL;
    tmpArr->currUsed = 0;
    tmpArr->currAlloc = 0;
    tmpArr->objSize = objSize;
    return 0;
  }

  /*
   * The array has already been allocated, so we don't
   * need to do anything.
   */
  if(tmpArr->dat)
    return 1;

  tmpArr->dat = (void *)calloc(numObj, objSize);
  if(!tmpArr->dat)
    return -1;

  /*
   * These internal variables will be used for when
   * we need to resize the array.
   */
  tmpArr->currUsed = 0;
  tmpArr->currAlloc = numObj;
  tmpArr->objSize = objSize;

  return 0;
}

/*
 * See if we need to dynamically resize this array, and then do so.
 *
 * On success, the value of currAlloc will be larger and the
 * capacity of the array will be larger, but no data in the actual
 * array changes.
 *
 * Return -1 on error (with no changes to the data structure),
 *         0 if no resizing was necessary,
 *         1 if resizing was necessary and succeeded.
 */
int TestAndGrowArray(growArray *arr, uint32_t numAdded)
{
  uint32_t newSize;
  void *newArray;

  if(!arr || !arr->objSize)
    return -1;

  /*
   * If arr->dat is NULL, that means the array was initialized
   *   to be zero-size.  Allocate the number of objects
   *   that want to be added.
   */
  if(!arr->dat) {
    arr->dat = (void *)calloc(numAdded, arr->objSize);
    if(!arr->dat)
      return -1;
    arr->currUsed = 0;
    arr->currAlloc = numAdded;
  }

  if((arr->currUsed + numAdded) <= arr->currAlloc)
    return 0;

  newSize = arr->currAlloc;
  do {
    newSize *= 2;
  } while((arr->currUsed + numAdded) > newSize);

  newArray = (void *)realloc(arr->dat, (newSize * arr->objSize));
  if(!newArray)
    return -1;

  arr->dat = newArray;
  arr->currAlloc *= 2;

  return 1;
}

/*
 * See if we can shrink the array, then do so.
 *
 * To prevent flip/flopping back and forth between
 *   growing and shrinking the array if one element
 *   is removed or inserted, we only shrink if the
 *   currUsed is <= 0.25 * currAlloc.
 *
 * Return -1 on error,
 *         0 on no resizing, and
 *         1 on resizing.
 */
int TestAndShrinkArray(growArray *arr)
{
  uint32_t newSize;
  void *newArray;

  if(!arr || !arr->objSize)
    return -1;

  if(!arr->currAlloc)
    return 0;

  if(arr->currUsed > (0.25 * arr->currAlloc))
    return 0;

  newSize = arr->currAlloc;
  do {
    newSize /= 2;
  } while(arr->currUsed > (0.25 * arr->currAlloc));

  newArray = (void *)realloc(arr->dat, (newSize * arr->objSize));
  if(!newArray)
    return -1;

  arr->dat = newArray;
  arr->currAlloc = newSize;

  return 1;
}

/*
 * Clean up the array and free all internal data structures.
 *
 * Return -1 on error (with no changes to the data structure),
 *         0 on success.
 */
int DelGrowArray(growArray **arr)
{
  if(!arr)
    return -1;

  if(!(*arr))
    return 0;

  if((*arr)->dat)
    free((*arr)->dat);

  free(*arr);
  *arr = NULL;

  return 0;
}
