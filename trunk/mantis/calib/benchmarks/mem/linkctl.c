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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/time.h>

#include "linkctl.h"
#include "linklib.h"
#include "utillog.h"
#include "workeropts.h"
#include "workersync.h"

/*
 * Simple commands to queue, start, or delete linked workers.
 */
int queue_link(gamut_opts *opts, char *llabel, char *attrs)
{
  int frc;
  int rc;

  if(!opts || !llabel || !strlen(llabel) || !attrs || !strlen(attrs))
    return -1;

  frc = -1;

  rc = lock_stats(opts);
  if(rc < 0) {
    goto fail_out;
  }

  rc = lock_link(opts);
  if(rc < 0) {
    goto stats_out;
  }

  rc = insert_link(opts, llabel, attrs);
  if(rc < 0) {
    s_log(G_WARNING, "Error inserting new link %s.\n", llabel);
  }
  else {
    s_log(G_DEBUG, "Inserted new link %s.\n", llabel);
    frc = 0;
  }

  rc = unlock_link(opts);
  if(rc < 0) {
    frc = -1;
  }

stats_out:
  rc = unlock_stats(opts);
  if(rc < 0) {
    frc = -1;
  }

fail_out:
  return frc;
}

int start_link(gamut_opts *opts, char *llabel)
{
  int frc;
  int lidx;
  int rc;

  if(!opts || !llabel || !strlen(llabel))
    return -1;

  frc  = -1;
  lidx = -1;

  rc = lock_stats(opts);
  if(rc < 0) {
    goto fail_out;
  }

  rc = lock_link(opts);
  if(rc < 0) {
    goto stats_out;
  }

  rc = find_link_by_label(opts, llabel, &lidx);
  if(!rc || (lidx < 0)) {
    s_log(G_WARNING, "Could not find link %s.\n", llabel);
    goto link_out;
  }
  else {
    s_log(G_DEBUG, "Found link %s at index %d.\n", llabel, lidx);
  }

  rc = start_queued_link(opts, lidx);
  if(rc < 0) {
    s_log(G_WARNING, "Error starting queued link %s.\n", llabel);
  }
  else {
    s_log(G_DEBUG, "Started queued link %s.\n", llabel);
    frc = 0;
  }

link_out:
  rc = unlock_link(opts);
  if(rc < 0) {
    frc = -1;
  }

stats_out:
  rc = unlock_stats(opts);
  if(rc < 0) {
    frc = -1;
  }

fail_out:
  return frc;
}

int del_link(gamut_opts *opts, char *llabel)
{
  int frc;
  int lidx;
  int rc;

  if(!opts || !llabel || !strlen(llabel))
    return -1;

  frc  = -1;
  lidx = -1;

  rc = lock_stats(opts);
  if(rc < 0) {
    goto clean_out;
  }

  rc = lock_link(opts);
  if(rc < 0) {
    goto stats_out;
  }

  rc = find_link_by_label(opts, llabel, &lidx);
  if(!rc || (lidx < 0)) {
    goto link_out;
  }

  rc = kill_link(opts, lidx);
  if(rc < 0) {
    s_log(G_WARNING, "Error killing link %s.\n", llabel);
  }
  else {
    s_log(G_DEBUG, "Killed link %s.\n", llabel);
    frc = 0;
  }

link_out:
  rc = unlock_link(opts);
  if(rc < 0) {
    frc = -1;
  }

stats_out:
  rc = unlock_stats(opts);
  if(rc < 0) {
    frc = -1;
  }

clean_out:
  return frc;
}
