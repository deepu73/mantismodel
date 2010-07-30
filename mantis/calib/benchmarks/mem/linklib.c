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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "constants.h"
#include "linklib.h"
#include "utilio.h"
#include "utillog.h"
#include "workerlib.h"
#include "workeropts.h"
#include "workersync.h"

static int find_open_slot(gamut_opts *gopts);
static int can_link_worker(shared_opts *shopts);

static int parse_link(gamut_opts *gopts, int lidx,
                      char *llabel, char *attrs);

/*
 * Insert a link, but don't actually start it yet.
 */
int insert_link(gamut_opts *gopts, char *llabel, char *attrs)
{
  int rc;
  int idx;

  if(!gopts || !llabel || !attrs)
    return -1;

  idx = find_open_slot(gopts);
  if(idx < 0) {
    s_log(G_WARNING, "Could not find open slot for new link.\n");
    goto clean_out;
  }
  else {
    s_log(G_DEBUG, "New slot %d for link %s.\n", idx, llabel);
  }

  rc = parse_link(gopts, idx, llabel, attrs);
  if(rc <= 0) {
    s_log(G_WARNING, "Error parsing new link %s.\n", llabel);
    idx = -1;
  }
  else {
    s_log(G_DEBUG, "Successfully parsed link %s.\n", llabel);
  }

clean_out:
  return idx;
}

/*
 * Start a set of linked workers.
 *   Tag all but the first as being link-waited.
 *   Then start the worker.
 */
int start_queued_link(gamut_opts *gopts, int lidx)
{
  int i;
  int frc;
  int rc;
  worker_link *wlink;

  if(!gopts || (lidx < 0) || (lidx >= MAX_LINKS))
    return -1;

  frc = -1;

  wlink = &gopts->wlinks.wlink[lidx];
  if(!wlink || !wlink->num_linked) {
    s_log(G_WARNING, "Error getting %dd linked workers.\n", lidx);
    goto clean_out;
  }

  /*
   * We work backwards to reduce the odds of the new worker
   *   leaping off, doing work, and delaying the start of the
   *   next worker in the link.
   */
  for(i = wlink->num_linked - 1;i >= 0;i--) {
    shared_opts *shopts;
    worker_data *wdata;

    wdata = &wlink->wdata[i];

    rc = lock_class(gopts, wdata->wcls);
    if(rc < 0) {
      goto clean_out;
    }

    /*
     * We'll lock the worker to tag it as being linkwait'ed.
     *   We'll also need to tag the worker as 'pending' now.
     */
    rc = lock_worker(gopts, wdata->wcls, wdata->worker_index);
    if(rc < 0) {
      (void)unlock_class(gopts, wdata->wcls);
      goto clean_out;
    }

    shopts = get_shared_opts(gopts, wdata->wcls, wdata->worker_index);
    if(!shopts) {
      (void)unlock_worker(gopts, wdata->wcls, wdata->worker_index);
      (void)unlock_class(gopts, wdata->wcls);
      goto clean_out;
    }

    if(i != 0) {
      shopts->linkwait = 1;
      gopts->wstats.workers_linkwait++;
    }
    shopts->pending = 1;
    gopts->wstats.workers_pending++;

    rc = unlock_worker(gopts, wdata->wcls, wdata->worker_index);
    if(rc < 0) {
      (void)unlock_class(gopts, wdata->wcls);
      goto clean_out;
    }

    s_log(G_DEBUG, "Set up linked worker %s.\n", shopts->label);

    /*
     * Now fire off the worker.
     */
    rc = start_queued_worker(gopts, wdata->wcls, wdata->worker_index);
    if(rc < 0) {
      s_log(G_WARNING, "Error starting linked worker %d.\n", i);
      (void)unlock_class(gopts, wdata->wcls);
      goto clean_out;
    }
    else {
      s_log(G_DEBUG, "Started linked worker %d.\n", i);
    }

    rc = unlock_class(gopts, wdata->wcls);
    if(rc < 0) {
      goto clean_out;
    }
  }

  frc = wlink->num_linked;

clean_out:
  return frc;
}

/*
 * Have a linked worker wait for the go-ahead to start.
 */
int link_start_wait(gamut_opts *gopts, worker_class wcls, int widx)
{
  int frc;
  int rc;
  shared_opts *shopts;

  if(!gopts || !is_valid_cls(wcls) || (widx < 0))
    return -1;

  frc = -1;

  /*
   * See if we need to wait for another linked worker
   *   to set us off.
   */
  rc = lock_worker(gopts, wcls, widx);
  if(rc < 0) {
    return -1;
  }

  shopts = get_shared_opts(gopts, wcls, widx);
  if(!shopts) {
    goto worker_out;
  }

  while(shopts->linkwait && !shopts->exiting) {
    rc = wait_worker(gopts, wcls, widx);
    if(rc < 0) {
      goto worker_out;
    }
  }

  frc = 0;

worker_out:
  rc = unlock_worker(gopts, wcls, widx);
  if(rc < 0) {
    frc = -1;
  }

  return frc;
}

/*
 * If we're linked to one or more other workers,
 *   this function will wait until it's our turn to do work
 *   or it's time for us to exit.
 *
 * This function returns -1 on error,
 *                        0 if we need to exit, or
 *                        1 if it's our turn.
 */
int link_next_wait(gamut_opts *gopts, worker_class wcls,
                   int widx, double epochs_per_link,
                   double *curr_epochs, int32_t *target_epochs)
{
  int rc;
  int frc;
  int bail_out;
  int32_t l_target_epochs;
  double l_curr_epochs;
  shared_opts *shopts;

  if(!gopts || !is_valid_cls(wcls) || (widx < 0)
     || !curr_epochs || !target_epochs)
  {
    return -1;
  }

  frc             = -1;
  bail_out        = 0;
  l_target_epochs = *target_epochs;
  l_curr_epochs   = *curr_epochs;

  shopts = get_shared_opts(gopts, wcls, widx);
  if(!shopts) {
    goto clean_out;
  }

  while(!l_target_epochs && !shopts->exiting && !bail_out) {
    shared_opts *link_shopts;
    worker_order worder;

    rc = lock_link(gopts);
    if(rc < 0) {
      break;
    }

    link_shopts = (shared_opts *)shopts->next_worker;

    /*
     * If the next worker is NULL, that means the rest of the link was
     *   removed when we weren't looking.  Don't bother to go through
     *   the rest of the motions.
     *
     * We will continue doing work, but don't wait in the future.
     */
    if(link_shopts == NULL) {
      *target_epochs = -1;
      *curr_epochs   = 0.0;

      rc = unlock_link(gopts);
      if(rc < 0) {
        frc = -1;
      }
      else {
        frc = 1;
      }
      break;
    }

    /*
     * If we got here, this means that there's another worker
     *   we'll need to lock.  Make sure we do it in the right order.
     */
    (void)init_worker_order(&worder);
    (void)append_worker(&worder, wcls, widx);
    (void)append_worker(&worder, link_shopts->wcls, link_shopts->widx);

    l_curr_epochs -= (int)l_curr_epochs;

    /*
     * Time to handoff to the next worker.
     * 1. Lock ourselves and the next worker.
     * 2. Signal the next worker
     * 3. Unlock the next worker
     * 4. Wait on ourselves
     */
    rc = lock_worker_order(gopts, &worder);
    if(rc < 0) {
      goto clean_out;
    }

    shopts->linkwait      = 1;
    link_shopts->linkwait = 0;

    /*
     * If there's a problem here, unlock the other worker first,
     *   then unlock ourselves when we get to the bottom.
     *   This might seem odd that we're unlocking in the wrong order,
     *   it's not a problem because we'll unlock the other in a sec.
     */
    rc = signal_worker(gopts, link_shopts->wcls, link_shopts->widx);
    if(rc < 0) {
      (void)unlock_worker(gopts, link_shopts->wcls, link_shopts->widx);
      bail_out = 1;
      goto worker_out;
    }

    /*
     * It's OK if we unlock in the wrong order, since that's
     *   equivalent to unlocking both, then just re-locking
     *   ourselves (see above).
     */
    rc = unlock_worker(gopts, link_shopts->wcls, link_shopts->widx);
    if(rc < 0) {
      bail_out = 1;
      goto worker_out;
    }

    /*
     * Unlock the link so we don't sleep while holding that lock.
     */
    rc = unlock_link(gopts);
    if(rc < 0) {
      bail_out = 1;
      goto worker_out;
    }

    while(shopts->linkwait && !shopts->exiting) {
      rc = wait_worker(gopts, wcls, widx);
      if(rc < 0) {
        bail_out = 1;
        goto worker_out;
      }
    }

    l_curr_epochs   += epochs_per_link;
    l_target_epochs  = (int)l_curr_epochs;

worker_out:
    rc = unlock_worker(gopts, wcls, widx);
    if(rc < 0) {
      frc = -1;
      goto clean_out;
    }
  }

  if(shopts->exiting) {
    frc = 0;
  }
  else if(l_target_epochs) {
    *curr_epochs   = l_curr_epochs;
    *target_epochs = l_target_epochs;
    frc = 1;
  }

clean_out:
  return frc;
}

/*
 * Remove ourselves from the link of workers.
 */
int link_remove(gamut_opts *gopts, worker_class wcls, int widx)
{
  int rc;
  int frc;
  shared_opts *shopts;
  worker_order worder;

  if(!gopts || !is_valid_cls(wcls) || (widx < 0))
    return -1;

  frc = -1;

  rc = init_worker_order(&worder);
  if(rc < 0) {
    goto clean_out;
  }

  rc = lock_link(gopts);
  if(rc < 0) {
    goto clean_out;
  }

  shopts = get_shared_opts(gopts, wcls, widx);
  if(!shopts) {
    goto link_out;
  }

  if(!shopts->prev_worker || !shopts->next_worker) {
    frc = 0;
    goto link_out;
  }

  rc = append_worker(&worder, wcls, widx);
  if(rc < 0) {
    goto link_out;
  }

  /*
   * Is there only one other worker left in this link?
   *   If so, lock it and set its links to NULL.
   */
  if(shopts->prev_worker == shopts->next_worker) {
    shared_opts *other;

    other = shopts->next_worker;

    rc = append_worker(&worder, other->wcls, other->widx);
    if(rc < 0) {
      goto link_out;
    }

    rc = lock_worker_order(gopts, &worder);
    if(rc < 0) {
      goto link_out;
    }

    other->prev_worker = NULL;
    other->next_worker = NULL;
    other->linkwait    = 0;

    rc = signal_worker(gopts, other->wcls, other->widx);
    if(rc < 0) {
      s_log(G_WARNING, "Error signalling 'next' worker.\n");
    }

    rc = unlock_worker_order(gopts, &worder);
    if(rc < 0) {
      goto link_out;
    }

    frc = 1;
  }
  else {  /* next_worker != prev_worker */
    /*
     * There are multiple workers left in this link.
     *   Remove ourselves from the doubly-linked list.
     */
    shared_opts *prev_w;
    shared_opts *next_w;

    prev_w = shopts->prev_worker;
    next_w = shopts->next_worker;

    rc = append_worker(&worder, prev_w->wcls, prev_w->widx);
    if(rc < 0) {
      goto link_out;
    }

    rc = append_worker(&worder, next_w->wcls, next_w->widx);
    if(rc < 0) {
      goto link_out;
    }

    rc = lock_worker_order(gopts, &worder);
    if(rc < 0) {
      goto link_out;
    }

    next_w->prev_worker = prev_w;
    prev_w->next_worker = next_w;
    next_w->linkwait    = 0;

    rc = signal_worker(gopts, next_w->wcls, next_w->widx);
    if(rc < 0) {
      s_log(G_WARNING, "Error signalling 'next' worker.\n");
    }

    frc = 1;

    rc = unlock_worker_order(gopts, &worder);
    if(rc < 0) {
      frc = -1;
    }
  }

link_out:
  rc = unlock_link(gopts);
  if(rc < 0) {
    frc = -1;
  }

clean_out:
  return frc;
}

/*
 * Find a link by the given label or worker.
 */
int find_link_by_label(gamut_opts *gopts, char *llabel, int *lidx)
{
  int i;
  int idx;

  if(!gopts || !llabel || !lidx || !strlen(llabel)) {
    s_log(G_WARNING, "Bad args passed to find_link_by_label.\n");
    return -1;
  }
  else {
    s_log(G_DEBUG, "Inside find_link_by_label.\n");
  }

  idx = -1;
  for(i = 0;i < MAX_LINKS;i++) {
    s_log(G_DEBUG, "Link %d: %u.\n", i,
                   gopts->wlinks.wlink[i].num_linked);
    if(!gopts->wlinks.wlink[i].num_linked)
      continue;

    s_log(G_DEBUG, "Link %d: %s ?= %s.\n", i, llabel,
                   gopts->wlinks.wlink[i].label);
    if(!strcmp(gopts->wlinks.wlink[i].label, llabel)) {
      idx = i;
      break;
    }
  }

  if(i == MAX_LINKS) {
    *lidx = -1;
    return 0;
  }
  else {
    *lidx = idx;
    return 1;
  }
}

int find_link_by_worker(gamut_opts *gopts, worker_class wcls,
                        int widx, int *lidx)
{
  int i;

  if(!gopts || !is_valid_cls(wcls) || (widx < 0) || !lidx)
    return -1;

  for(i = 0;i < MAX_LINKS;i++) {
    int j;
    int num_linked;
    worker_link *wlink;

    if(!gopts->wlinks.wlink[i].num_linked) {
      continue;
    }

    wlink      = &gopts->wlinks.wlink[i];
    num_linked = wlink->num_linked;
    for(j = 0;j < num_linked;j++) {
      if((wlink->wdata[j].worker_index == widx)
         &&
         (wlink->wdata[j].wcls == wcls)
        )
      {
        break;
      }
    }

    /*
     * If we didn't hit the end, we found ourselves.  Bail.
     */
    if(j != num_linked) {
      break;
    }
  }

  if(i != MAX_LINKS) {
    *lidx = i;
    return 1;
  }
  else {
    *lidx = -1;
    return 0;
  }
}


/*
 * Kill a set of linked workers.
 */
int kill_link(gamut_opts *gopts, int lidx)
{
  int i;
  int frc;
  int rc;
  worker_link *wlink;

  if(!gopts || (lidx < 0) || (lidx >= MAX_LINKS))
    return -1;

  frc = -1;

  wlink = &gopts->wlinks.wlink[lidx];
  if(!wlink || !wlink->num_linked) {
    goto clean_out;
  }

  /*
   * We work backwards to catch the link as it progresses.
   */
  for(i = wlink->num_linked - 1;i >= 0;i--) {
    worker_data *wdata;

    wdata = &wlink->wdata[i];

    rc = lock_class(gopts, wdata->wcls);
    if(rc < 0) {
      goto clean_out;
    }

    rc = kill_worker(gopts, wdata->wcls, wdata->worker_index);
    if(rc < 0) {
      s_log(G_WARNING, "Error killing linked worker (%d, %d).\n",
                       wdata->wcls, wdata->worker_index);
      (void)unlock_class(gopts, wdata->wcls);
      goto clean_out;
    }

    rc = unlock_class(gopts, wdata->wcls);
    if(rc < 0) {
      goto clean_out;
    }
  }

  frc = 0;

clean_out:
  return frc;
}

static int find_open_slot(gamut_opts *gopts)
{
  int i;
  int idx;

  if(!gopts)
    return -1;

  idx = -1;
  for(i = 0;i < MAX_LINKS;i++) {
    if(!gopts->wlinks.wlink[i].num_linked) {
      break;
    }
  }
  if(i != MAX_LINKS) {
    idx = i;
  }

  return idx;
}

static int can_link_worker(shared_opts *shopts)
{
  if(!shopts)
    return -1;

  if(!shopts->used)
    return 0;

  if(shopts->pending || shopts->waiting)
    return 0;

  if(shopts->running || shopts->exiting)
    return 0;

  if(shopts->linked)
    return 0;

  return 1;
}

static int parse_link(gamut_opts *gopts, int lidx,
                      char *llabel, char *attrs)
{
  char *args[MAX_LINKLEN];
  int i;
  int frc;
  int rc;
  int nargs;
  uint64_t link_work[MAX_LINKLEN];
  worker_link wlink;

  if(!gopts || (lidx < 0) || (lidx >= MAX_LINKS)
     || !llabel || !strlen(llabel) || !attrs || !strlen(attrs))
  {
    s_log(G_WARNING, "Invalid parameters passed to parse_link.\n");
    return -1;
  }

  frc = -1;
  memset(&wlink,    0, sizeof(wlink));
  memset(link_work, 0, sizeof(link_work));

  nargs = split(",", attrs, args, MAX_LINKLEN, ws_is_delim);
  if(nargs < 1) {
    s_log(G_WARNING, "Invalid attributes passed to parse_link.\n");
    goto clean_out;
  }

  /*
   * Links are complex and demand synchronization across
   *   multiple classes and workers.  Lock everything down
   *   so nothing changes while we're doing this.
   *
   * This is a lot of overhead, but it shouldn't happen that often.
   *   We can afford for this to happen occasionally.
   */
  rc = lock_class(gopts, CLS_CPU);
  if(rc < 0) {
    goto clean_out;
  }

  rc = lock_class(gopts, CLS_MEM);
  if(rc < 0) {
    goto cpu_out;
  }

  rc = lock_class(gopts, CLS_DISK);
  if(rc < 0) {
    goto mem_out;
  }

  rc = lock_class(gopts, CLS_NET);
  if(rc < 0) {
    goto disk_out;
  }

  for(i = 0;i < nargs;i++) {
    char *q;
    char *wargs[2];
    int nwargs;
    int widx;
    worker_class wcls;
    shared_opts *shopts;

    nwargs = split("=", args[i], wargs, 2, ws_is_delim);
    if(nwargs != 2) {
      goto net_out;
    }

    wcls = CLS_ALL;
    rc = find_worker_by_label(gopts, &wcls, wargs[0], &widx);
    if(!rc) {
      goto net_out;
    }
    else if((wcls == CLG_ERROR) || (widx < 0)) {
      goto net_out;
    }

    s_log(G_DEBUG, "Found worker %s at (%d, %d).\n", wargs[0],
                   wcls, widx);

    errno = 0;
    link_work[i] = (uint64_t)strtoull(wargs[1], &q, 10);
    if(errno || (wargs[1] == q)) {
      goto net_out;
    }

    shopts = get_shared_opts(gopts, wcls, widx);
    if(!shopts) {
      s_log(G_WARNING, "Could not get shared opts for %s.\n", wargs[0]);
      goto net_out;
    }

    rc = can_link_worker(shopts);
    if(rc <= 0) {
      s_log(G_WARNING, "Cannot link worker %s.\n", wargs[0]);
      goto net_out;
    }

    wlink.wdata[i].wcls          = wcls;
    wlink.wdata[i].worker_index  = widx;
    link_work[i]                *= get_multiplier(q);
  }

  /*
   * If we got here, all the labels checked out OK,
   *   and we're OK to link this worker.
   */
  {
    shared_opts *first_w;
    shared_opts *last_w;
    shared_opts *prev_w;
    shared_opts *next_w;

    first_w = NULL;
    last_w  = NULL;
    prev_w  = NULL;
    next_w  = NULL;

    /*
     * First we go forwards to establish all the 'previous' links.
     */
    for(i = 0;i < nargs;i++) {
      worker_data *wdata;
      shared_opts *shopts;

      wdata = &wlink.wdata[i];
      
      rc = lock_worker(gopts, wdata->wcls, wdata->worker_index);
      if(rc < 0) {
        goto net_out;
      }

      shopts = get_shared_opts(gopts, wdata->wcls, wdata->worker_index);
      if(!shopts) {
        (void)unlock_worker(gopts, wdata->wcls, wdata->worker_index);
        goto net_out;
      }

      if(i == 0) {
        /*
         * The first one.  We can't do anything yet.
         */
        first_w = shopts;
        prev_w  = shopts;
      }
      else if(i == (nargs - 1)) {
        /*
         * The last one.  We can fill in both, and save
         *   ourselves for later.
         */
        shopts->prev_worker = prev_w;
        shopts->next_worker = first_w;
        last_w              = shopts;
      }
      else {
        /*
         * Something in the middle.  We can only fill in
         *   the previous one.
         */
        shopts->prev_worker = prev_w;
        prev_w              = shopts;
      }

      rc = unlock_worker(gopts, wdata->wcls, wdata->worker_index);
      if(rc < 0) {
        goto net_out;
      }
    }

    /*
     * Now go backwards to establish all the 'next' links.
     *   Also fill in the amount of work they need to do,
     *   and tag them as being linked.
     */
    for(i = nargs - 1;i >= 0;i--) {
      worker_data *wdata;
      shared_opts *shopts;

      wdata = &wlink.wdata[i];
      
      rc = lock_worker(gopts, wdata->wcls, wdata->worker_index);
      if(rc < 0) {
        goto net_out;
      }

      shopts = get_shared_opts(gopts, wdata->wcls, wdata->worker_index);
      if(!shopts) {
        (void)unlock_worker(gopts, wdata->wcls, wdata->worker_index);
        goto net_out;
      }

      if(i == 0) {
        /*
         * The first one.  Fill in both.
         */
        shopts->prev_worker = last_w;
        shopts->next_worker = next_w;
      }
      else if(i == (nargs - 1)) {
        /*
         * The last one.  All we need to do is establish us as 'next'.
         */
        next_w = shopts;
      }
      else {
        /*
         * Something in the middle.  We can only fill in
         *   the 'next' one.
         */
        shopts->next_worker = next_w;
        next_w              = shopts;
      }

      shopts->linked    = 1;
      shopts->link_work = link_work[i];

      gopts->wstats.workers_linked++;

      rc = unlock_worker(gopts, wdata->wcls, wdata->worker_index);
      if(rc < 0) {
        goto net_out;
      }
    }

    /*
     * Fill in the meta-data for this link.
     */
    strncpy(wlink.label, llabel, sizeof(wlink.label));
    wlink.num_linked = nargs;

    /*
     * Now copy everything over to the actual link structure.
     */
    memcpy(&gopts->wlinks.wlink[lidx], &wlink, sizeof(worker_link));

    s_log(G_DEBUG, "Link label: %s\n", gopts->wlinks.wlink[lidx].label);
    s_log(G_DEBUG, "# linked:   %u\n",
                   gopts->wlinks.wlink[lidx].num_linked);

    /*
     * If we got here, then everything went well.
     *   Give a return code that indicates success.
     */
    frc = nargs;
  }

  /*
   * Now unlock everything and bail out.
   */
net_out:
  rc = unlock_class(gopts, CLS_NET);
  if(rc < 0) {
    frc = -1;
  }

disk_out:
  rc = unlock_class(gopts, CLS_DISK);
  if(rc < 0) {
    frc = -1;
  }

mem_out:
  rc = unlock_class(gopts, CLS_MEM);
  if(rc < 0) {
    frc = -1;
  }

cpu_out:
  rc = unlock_class(gopts, CLS_CPU);
  if(rc < 0) {
    frc = -1;
  }

clean_out:
  return frc;
}
