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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "calibrate.h"
#include "constants.h"
#include "diskworker.h"
#include "linklib.h"
#include "utilrand.h"
#include "utillog.h"
#include "workerctl.h"
#include "workerlib.h"
#include "workeropts.h"
#include "workersync.h"

/*
 * Do the work for this epoch.
 */
static int diskwork(gamut_opts *gopts, dio_opts *dio,
                    int fd, char *buf, iorange *iomix,
                    int64_t *target_diskio, double blocks_per_epoch,
                    double *curr_blocks, uint32_t *sync_count);

static int init_workfile(dio_opts *dio);
static int next_dio_operation(int fd, char *buf, dio_opts *dio,
                              iorange *ior);
static int close_workfile(int fd, dio_opts *dio);
static void print_iostats(int64_t total_usec, dio_opts *dio, char *tag);

static void create_random_block(char *buf, uint32_t blksize);
static uint64_t time_create_random_block(char *buf, uint32_t blksize);

/*
 * Fire off an I/O worker to do a certain number of I/Os per second.
 */
void* diskworker(void *opts)
{
  char *buf;
  int fd;
  int rc;
  int dio_index;
  int32_t target_epochs;
  uint32_t sync_count;
  int64_t link_waittime;
  int64_t target_diskio;
  uint64_t next_deadline;
  double blocks_per_epoch;
  double curr_blocks;
  double epochs_per_link;
  double curr_epochs;
  iorange iomix;
  dio_opts *dio;
  gamut_opts *gopts;
  struct timeval start;
  struct timeval finish;
  struct timeval finish_time;

  if(!opts)
    return NULL;

  gopts = (gamut_opts *)opts;

  dio_index = worker_register(gopts, CLS_DISK);
  if(dio_index < 0) {
    return NULL;
  }
  else {
    dio = &gopts->disk_io[dio_index];
  }

  /*
   * See if we need to wait for another linked worker
   *   to set us off.
   */
  rc = link_start_wait(gopts, CLS_DISK, dio_index);
  if(rc < 0) {
    return NULL;
  }

  (void)gettimeofday(&dio->shopts.start_time, NULL);
  dio->total_diskio = 0;
  dio->shopts.missed_deadlines = 0;
  dio->shopts.missed_usecs     = 0;
  dio->shopts.total_deadlines  = 0;
  memset(dio->num_diskio, 0, sizeof(dio->num_diskio));
  memset(dio->io_usec,    0, sizeof(dio->io_usec));

  buf           = NULL;
  fd            = -1;
  link_waittime = 0;

restart:
  (void)gettimeofday(&dio->shopts.mod_time, NULL);
  test_and_close(fd);
  dio->shopts.dirty = 0;

  sync_count       = 0;
  target_diskio    = 0;
  epochs_per_link  = 0.0;
  curr_epochs      = 0.0;

  rc = validate_worker_opts(opts, CLS_DISK, dio_index);
  if(rc <= 0)
  {
    s_log(G_WARNING, "%s has invalid disk settings.\n",
                     dio->shopts.label);
    goto clean_out;
  }

  /*
   * Calculate the first deadline and the final deadline (if necessary).
   */
  {
    struct timeval tv;

    (void)gettimeofday(&tv, NULL);

    /*
     * If there's a time limit, let's calculate it.
     */
    if(dio->shopts.exec_time) {
      finish_time.tv_sec  = tv.tv_sec + dio->shopts.exec_time;
      finish_time.tv_usec = tv.tv_usec;
    }
    else {
      finish_time.tv_sec  = 0;
      finish_time.tv_usec = 0;
    }

    next_deadline  = tv.tv_usec;
    next_deadline += tv.tv_sec * US_SEC;
  }

  /*
   * We do a 'realloc' here instead of a malloc, since it
   *   will work the first time through (when buf is NULL)
   *   and during subsequent passes (when buf is not NULL).
   */
  buf = (char *)realloc(buf, (size_t)dio->blksize);
  if(!buf) {
    s_log(G_WARNING, "%s unable to allocate a %u-byte block.\n",
                     dio->shopts.label, dio->blksize);
    goto clean_out;
  }

  /*
   * Calculate the ranges for the different I/O types
   *   so we can perform the random mix as specified.
   */
  iomix.reads.min  = 0;
  iomix.reads.max  = dio->iomix.numrds - 1;
  iomix.writes.min = iomix.reads.max + 1;
  iomix.writes.max = iomix.writes.min + dio->iomix.numwrs - 1;
  iomix.seeks.min  = iomix.writes.max + 1;
  iomix.seeks.max  = iomix.seeks.min + dio->iomix.numsks - 1;
  iomix.maxval     = iomix.seeks.max;

  /*
   * With the blocksize and the I/O rate we can figure out
   *   how many blocks we need to perform I/O on per epoch.
   */
  blocks_per_epoch  = (float)dio->iorate / dio->blksize;
  blocks_per_epoch /= WORKER_EPOCHS_PER_SEC;

  s_log(G_DEBUG, "%s disk I/O rate of %.4f blocks/epoch.\n",
                 dio->shopts.label, blocks_per_epoch);

  /*
   * How long does it take to fill a block with random stuff?
   *
   * NOTE: At some point we might think about randomly re-filling
   *       the buffer at different times.
   */
  {
    uint64_t tcrb;

    tcrb = time_create_random_block(buf, dio->blksize);
    s_log(G_DEBUG, "Took %llu usec to fill random %u-byte block.\n",
                   tcrb, dio->blksize);
  }

  /* Initialize the work file */
  fd = init_workfile(dio);
  if(fd < 0) {
    goto clean_out;
  }

  /*
   * See how often we have to sync the buffers to disk.
   */
  if(dio->sync_f)
  {
    sync_count = dio->sync_f;
  }
  else
  {
    dio->sync_f = 1;
    sync_count  = dio->sync_f;
  }

  /*
   * Calculate the total number of disk I/O operations
   *   that this worker will perform.
   */
  if(dio->shopts.max_work)
  {
    target_diskio = dio->shopts.max_work / dio->blksize;
    if(!target_diskio)
      target_diskio = 1;
  }
  else
  {
    target_diskio = -1;
  }

  /*
   * Make sure that if we've been asked to be part of a link,
   *   that we've got enough information to be sure that the
   *   link will actually work.
   */
  if(dio->shopts.link_work && dio->shopts.next_worker)
  {
    shared_opts *link_shopts;

    epochs_per_link = (double)dio->shopts.link_work
                      / (blocks_per_epoch * dio->blksize);
    curr_epochs     = epochs_per_link;
    target_epochs   = (int32_t)curr_epochs;

    link_shopts      = (shared_opts *)dio->shopts.next_worker;

    s_log(G_DEBUG, "Will do %.2f epochs per link, handing off to %s.\n",
                   epochs_per_link, link_shopts->label);
  }
  else
  {
    target_epochs = -1;
  }

  /*
   * Each time through, perform these operations in this order:
   * 1. Calculate the next deadline
   * 2. Perform the I/O, keeping tabs on the right I/O mix
   * 3. If we're linked with another worker, see if it's handoff time
   * 4. See if it's time to exit
   * 5. Sleep, if there's enough time
   */
  curr_blocks = 0.0;
  (void)gettimeofday(&start, NULL);
  while(!dio->shopts.exiting) {
    struct timeval now;

    /* Steps 1 & 2 for an unlinked worker */
    if(target_epochs < 0) {
      /* Step 1 */
      next_deadline += US_PER_WORKER_EPOCH;

      /* Step 2 */
      rc = diskwork(gopts, dio, fd, buf, &iomix, &target_diskio,
                    blocks_per_epoch, &curr_blocks, &sync_count);
      if(rc < 0) {
        s_log(G_WARNING, "Error doing diskwork.  Exiting.\n");
        dio->shopts.exiting = 1;
        break;
      }
      else if(!rc) {
        s_log(G_DEBUG, "Diskwork says we need to bail.\n");
        break;
      }
    }
    else { /* (target_epochs >= 0), a linked worker */
      if(target_epochs > 0) {
        /* Step 1 */
        next_deadline += US_PER_WORKER_EPOCH;

        /* Step 2 */
        rc = diskwork(gopts, dio, fd, buf, &iomix, &target_diskio,
                      blocks_per_epoch, &curr_blocks, &sync_count);
        if(rc < 0) {
          s_log(G_WARNING, "Error doing diskwork.  Exiting.\n");
          dio->shopts.exiting = 1;
          break;
        }
        else if(!rc) {
          s_log(G_DEBUG, "Diskwork says we need to bail.\n");
          break;
        }

        target_epochs--;
      }

      /*
       * If we do have to wait, make sure we don't over-work
       *   when it's our turn.  Move the next_deadline backward
       *   by as much time as we spend waiting.
       */
      if(!target_epochs) {
        int64_t timediff;
        struct timeval b_link;

        (void)gettimeofday(&b_link, NULL);
        rc = link_next_wait(gopts, CLS_DISK, dio_index, epochs_per_link,
                            &curr_epochs, &target_epochs);
        if(rc < 0) {
          s_log(G_WARNING, "Error in link_next_wait.\n");
        }
        else if(!rc) {
          s_log(G_DEBUG, "We need to exit (link_wait says so).\n");
          break;
        }
        else {
          struct timeval f_link;

          (void)gettimeofday(&f_link, NULL);
          s_log(G_DEBUG, "EL %.2f  CE %.2f  TE %d\n",
                         epochs_per_link, curr_epochs, target_epochs);

          timediff = calculate_timediff(&b_link, &f_link);
          next_deadline += timediff;
          link_waittime += timediff;
          s_log(G_DEBUG, "Moved next deadline backward by %lld usec.\n",
                         timediff);
        }
      }
    }

    /* Step 4 & 5 */
    (void)gettimeofday(&now, NULL);

    if(!finish_time.tv_sec
       || (finish_time.tv_sec > now.tv_sec)
       || ((finish_time.tv_sec == now.tv_sec)
           && (finish_time.tv_usec > now.tv_usec)
          )
      )
    {
      int64_t time_diff;
      int64_t current_time;

      current_time  = now.tv_usec;
      current_time += now.tv_sec * US_SEC;

      time_diff = next_deadline - current_time;

      /* Is there enough time to sleep? */
      if(current_time < next_deadline)
      {
        if(time_diff > MIN_SLEEP_US) {
          struct timeval sleeptv;

          sleeptv.tv_sec  = time_diff / US_SEC;
          sleeptv.tv_usec = time_diff - (sleeptv.tv_sec * US_SEC);
          (void)select(0, (fd_set *)NULL, (fd_set *)NULL,
                       (fd_set *)NULL, &sleeptv);
        }
      }
      else
      {
        dio->shopts.missed_deadlines++;
        dio->shopts.missed_usecs += (current_time - next_deadline);
      }
      dio->shopts.total_deadlines++;
    }
    else {
        dio->shopts.exiting = 1;
        break;
    }

    /*
     * Do we need to adjust our I/O in any way?
     */
    if(dio->shopts.dirty) {
      s_log(G_INFO, "%s reloading values.\n", dio->shopts.label);
      goto restart;
    }
  }

  /*
   * If we did any sort of I/O, make sure we sync before exit.
   */
  if(dio->num_diskio[C_IOREAD] || dio->num_diskio[C_IOWRITE]) {
    s_log(G_DEBUG, "Starting to sync %s.\n", dio->file);
    rc = fsync(fd);
    if(rc < 0) {
      s_log(G_WARNING, "Error sync'ing file %s.\n", dio->file);
    }
    s_log(G_DEBUG, "Sync of %s done.\n", dio->file);
  }

  /*
   * Make sure to take this time /after/ we sync.
   */
  (void)gettimeofday(&finish, NULL);

  rc = lock_stats(gopts);
  if(rc < 0) {
    goto clean_out;
  }
  gopts->wstats.workers_exiting++;
  (void)unlock_stats(gopts);

clean_out:
  /*
   * Clean up all state behind us.
   */
  if(buf)
    free(buf);

  (void)close_workfile(fd, dio);

  if(dio->file)
    free(dio->file);
  dio->file = NULL;

  if(dio->num_diskio[C_IOREAD] || dio->num_diskio[C_IOWRITE])
  {
    int64_t total_usec;
    uint64_t avg_miss_time;

    total_usec    = calculate_timediff(&start, &finish);
    if(dio->shopts.missed_deadlines) {
      avg_miss_time = dio->shopts.missed_usecs
                      / dio->shopts.missed_deadlines;
    }
    else {
      avg_miss_time = 0;
    }

    print_iostats(total_usec, dio, "total");

    if(link_waittime) {
      print_iostats(total_usec - link_waittime, dio, "work");
    }

    s_log(G_INFO, "%s missed %llu of %llu deadlines by %llu usecs (avg).\n",
                  dio->shopts.label, dio->shopts.missed_deadlines,
                  dio->shopts.total_deadlines, avg_miss_time);
  }

  /*
   * Remove ourselves from any links.
   */
  rc = link_remove(gopts, CLS_DISK, dio_index);
  if(rc < 0) {
    s_log(G_WARNING, "Error removing %s from any link.\n",
                     dio->shopts.label);
  }

  (void)worker_unregister(gopts, CLS_DISK);

  return NULL;
}

/*
 * Do the work for this epoch.
 */
static int diskwork(gamut_opts *gopts, dio_opts *dio,
                    int fd, char *buf, iorange *iomix,
                    int64_t *target_diskio, double blocks_per_epoch,
                    double *curr_blocks, uint32_t *sync_count)
{
  int      rc;
  off_t    endoffile;
  uint32_t num_seeks;
  uint32_t l_sync_count;
  int64_t  l_target_diskio;
  uint64_t target_blocks;
  double   l_curr_blocks;

  if(!gopts || !dio || (fd < 0) || !buf || !iomix || !target_diskio
     || (blocks_per_epoch < 0) || !curr_blocks || !sync_count
    )
  {
    return -1;
  }

  /*
   * Make local copies of the variables
   */
  l_sync_count    = *sync_count;
  l_curr_blocks   = *curr_blocks;
  l_target_diskio = *target_diskio;

  num_seeks      = 0;
  endoffile      = (off_t)(dio->blksize * dio->nblks);
  l_curr_blocks += blocks_per_epoch;
  target_blocks  = (uint64_t)l_curr_blocks;

  while(target_blocks && (num_seeks < MAX_DISK_SEEKS)) {
    rc = next_dio_operation(fd, buf, dio, iomix);
    if(rc < 0) {
      s_log(G_WARNING, "%s: Error in I/O operation.\n",
                       dio->shopts.label);
      break;
    }
    else {
      if(rc > 0) { /* Actual I/O */
        dio->total_diskio += dio->blksize;
        target_blocks--;
        l_sync_count--;

        /*
         * See if we need to fsync() this time around.
         */
        if(!l_sync_count) {
          (void)fsync(fd);
          l_sync_count = dio->sync_f;
        }

        /*
         * Make sure we're not at the end of the file,
         *   as the next read will fail.
         */
        {
          off_t currpos;
          
          currpos = lseek(fd, (off_t)0, SEEK_CUR);
          if(currpos >= endoffile) {
            s_log(G_DLOOP, "%s: Resetting fd position%s.\n",
                           dio->shopts.label,
                           ((currpos == endoffile) ? "" : " HARD"));
            (void)lseek(fd, (off_t)0, SEEK_SET);
          }
        }

        if(l_target_diskio > 0) {
          l_target_diskio--;
          if(!l_target_diskio) {
            dio->shopts.exiting = 1;
            break;
          }
        }
      }
      else {  /* A seek */
        num_seeks++;
      }
    }
  }

  l_curr_blocks -= (uint64_t)l_curr_blocks;

  /*
   * Copy all local variables back.
   */
  *sync_count    = l_sync_count;
  *curr_blocks   = l_curr_blocks;
  *target_diskio = l_target_diskio;

  /*
   * Find out if we need return error, return exit, or return success.
   */
  if(dio->shopts.exiting) {
    return 0;
  }
  else if(target_blocks && (num_seeks < MAX_DISK_SEEKS)) {
    /* We bailed out early.  There's a problem. */
    s_log(G_WARNING, "Diskwork: TB %llu NS %u MDS %d\n",
                     target_blocks, num_seeks, MAX_DISK_SEEKS);
    return -1;
  }
  else {
    /* Everything seems to have gone OK */
    return 1;
  }
}

static int init_workfile(dio_opts *dio)
{
  int fd;
  int flags;
  mode_t mode;

  if(!dio)
    return -1;

  if(dio->iomix.numrds) {
    if(dio->iomix.numwrs)
      flags = O_RDWR;
    else
      flags = O_RDONLY;
  }
  else {
    flags = O_WRONLY;
  }

  /* What other flags should be in place? */
  if(flags != O_RDONLY) {
    flags |= O_CREAT;
    flags |= O_TRUNC;
  }

  /* Always open in mode 640 for security reasons */
  mode = S_IRUSR | S_IWUSR | S_IRGRP;

  errno = 0;
  fd = open(dio->file, flags, mode);
  if(fd < 0) {
    s_log(G_WARNING, "%s: Error opening file \"%s\" "
                     "with flags %x and mode %x: %s.\n", dio->shopts.label,
                     dio->file, flags, mode, strerror(errno));
    return -1;
  }

  /*
   * If this is a new file we're creating, fill it with junk
   *   so reads don't return nothing.
   */
  if(flags & O_CREAT)
  {
    off_t eof;
    off_t currpos;
    size_t rc;

    eof = dio->nblks * dio->blksize;
    currpos = lseek(fd, eof, SEEK_SET);
    if(currpos != eof) {
      s_log(G_WARNING, "%s: Seek error initializing file "
                       "contents (position %d of %d).\n",
                       dio->shopts.label, (int)currpos, (int)eof);
      goto fail_out;
    }

    rc = write(fd, "x", 1);
    if(rc != 1) {
      s_log(G_WARNING, "%s: Write error initializing file "
                       "contents (byte %d of %d).\n",
                       dio->shopts.label, rc, 1);
      goto fail_out;
    }

    /* Reset the file descriptor to the start of the file */
    (void)lseek(fd, (off_t)0, SEEK_SET);
  }

  return fd;

fail_out:
  (void)close(fd);
  (void)unlink(dio->file);

  return -1;
}

static int next_dio_operation(int fd, char *buf, dio_opts *dio,
                              iorange *ior)
{
  int frc;
  int operr;
  int ioname;
  int32_t iotype;
  uint32_t blksize;
  uint32_t numblks;
  size_t numbytes;
  struct timeval bt, ft;

  if((fd < 0) || !buf || !ior || !dio)
  {
    return -1;
  }

  frc      = -1;
  blksize  = dio->blksize;
  numblks  = dio->nblks;
  errno    = 0;
  ioname   = -1;
  iotype   = RandInt(ior->maxval);
  numbytes = 0;
  if((iotype >= ior->seeks.min) && (iotype <= ior->seeks.max)) {
    uint32_t newblk;
    off_t pos;
    off_t newpos;

    ioname = C_IOSEEK;
    newblk = RandInt(numblks - 1);
    pos    = newblk * blksize;

    (void)gettimeofday(&bt, NULL);
    newpos = lseek(fd, pos, SEEK_SET);
    operr  = errno;
    (void)gettimeofday(&ft, NULL);
    if(newpos != pos) {
      s_log(G_WARNING, "%s: Error seeking to %u (%u): %s.\n",
                       dio->shopts.label, (uint32_t)pos,
                       (uint32_t)newpos, strerror(operr));
      goto fail_out;
    }
  }
  else if((iotype >= ior->reads.min) && (iotype <= ior->reads.max)) {
    ioname = C_IOREAD;

    (void)gettimeofday(&bt, NULL);
    numbytes = read(fd, buf, (size_t)blksize);
    operr    = errno;
    (void)gettimeofday(&ft, NULL);
  }
  else if((iotype >= ior->writes.min) && (iotype <= ior->writes.max)) {
    ioname = C_IOWRITE;

    (void)gettimeofday(&bt, NULL);
    numbytes = write(fd, buf, (size_t)blksize);
    operr    = errno;
    (void)gettimeofday(&ft, NULL);
  }
  else {
    s_log(G_WARNING, "%s: Unknown I/O type created: %d.\n",
                     dio->shopts.label, iotype);
    goto fail_out;
  }

  if((ioname != C_IOSEEK) && (numbytes != blksize)) {
    s_log(G_WARNING, "%s: Only %s %d of %u bytes: %s.\n",
                     dio->shopts.label, (ioname ? "wrote" : "read"),
                     numbytes, blksize, strerror(operr));
    goto fail_out;
  }

  dio->num_diskio[ioname] += 1;
  dio->io_usec[ioname]    += calculate_timediff(&bt, &ft);

  if(ioname == C_IOSEEK) {
    frc = 0;
  }
  else {
    frc = 1;
  }

fail_out:
  return frc;
}

static int close_workfile(int fd, dio_opts *dio)
{
  if((fd < 0) || !dio)
    return -1;

  (void)close(fd);

  /*
   * Only delete the file if we know we created it in the first place.
   *   If we wrote to the file and created it because it didn't exist,
   *   then it shouldn't exist when we exit.
   */
  if((dio->create == C_IFNEXIST) && dio->iomix.numwrs) {
    (void)unlink(dio->file);
  }

  return 0;
}

static void print_iostats(int64_t total_usec, dio_opts *dio, char *tag)
{
  char iorate[SMBUFSIZE];
  int64_t total_io;
  double iotime;

  if(!dio || !tag)
    return;

  if(!total_usec)
    return;

  if(!strlen(tag)) {
    tag = "total";
  }

  /*
   * Print a number of statistics about the I/O rates
   * 1. Overall I/O and I/O rates
   * 2. Overall I/O and I/O rates excluding seek time
   * 3. Read I/O and I/O rates
   * 4. Write I/O and I/O rates
   * 5. Seek time and seek rates
   */

  total_io  = dio->num_diskio[C_IOREAD] + dio->num_diskio[C_IOWRITE];
  total_io *= dio->blksize;
  iotime    = (double)total_usec / US_SEC;

  /* Number 1 */
  print_scaled_number(iorate, SMBUFSIZE,
                      (uint64_t)(total_io / iotime), 1);
  s_log(G_NOTICE, "%s did %llu disk I/O in %.4f sec "
                  "at %sps (with seek) (%s).\n", dio->shopts.label,
                  total_io, iotime, iorate, tag);

  /* Number 2 */
  if(dio->io_usec[C_IOSEEK] != total_usec)
  {
    double seektime;

    seektime = (double)dio->io_usec[C_IOSEEK] / US_SEC;
    print_scaled_number(iorate, SMBUFSIZE,
                        (uint64_t)(total_io / (iotime - seektime)), 1);
    s_log(G_NOTICE, "%s did %llu disk I/O in %.4f sec "
                    "at %sps (without seek) (%s).\n", dio->shopts.label,
                    total_io, iotime - seektime, iorate, tag);
  }

  /* Number 3 */
  if(dio->num_diskio[C_IOREAD] && dio->io_usec[C_IOREAD])
  {
    double readtime;
    int64_t read_io;

    read_io  = dio->num_diskio[C_IOREAD] * dio->blksize;
    readtime = (double)dio->io_usec[C_IOREAD] / US_SEC;
    print_scaled_number(iorate, SMBUFSIZE,
                        (uint64_t)(read_io / readtime), 1);
    s_log(G_NOTICE, "%s did %llu bytes read in %.4f sec "
                    "at %sps (%s).\n", dio->shopts.label,
                    read_io, readtime, iorate, tag);
  }

  /* Number 4 */
  if(dio->num_diskio[C_IOWRITE] && dio->io_usec[C_IOWRITE])
  {
    double writetime;
    int64_t write_io;

    write_io  = dio->num_diskio[C_IOWRITE] * dio->blksize;
    writetime = (double)dio->io_usec[C_IOWRITE] / US_SEC;
    print_scaled_number(iorate, SMBUFSIZE,
                        (uint64_t)(write_io / writetime), 1);
    s_log(G_NOTICE, "%s did %llu bytes written in %.4f sec "
                    "at %sps (%s).\n", dio->shopts.label,
                    write_io, writetime, iorate, tag);
  }

  /* Number 5 */
  if(dio->num_diskio[C_IOSEEK] && dio->io_usec[C_IOSEEK])
  {
    double seektime;
    int64_t seek_io;

    seek_io  = dio->num_diskio[C_IOSEEK];
    seektime = (double)dio->io_usec[C_IOSEEK] / US_SEC;
    print_scaled_number(iorate, SMBUFSIZE,
                        (uint64_t)(seek_io / seektime), 0);
    s_log(G_NOTICE, "%s did %llu disk seeks in %.4f sec "
                    "at %s seeks/sec (%s).\n", dio->shopts.label,
                    seek_io, seektime, iorate, tag);
  }
}

static void create_random_block(char *buf, uint32_t blksize)
{
  uint32_t i;
  uint32_t v;
  uint32_t num_rand;

  if(!buf || !blksize)
    return;

  num_rand = blksize / sizeof(uint32_t);
  for(i = 0;i < num_rand;i++) {
    v = randomMT();
    memcpy((uint32_t *)&buf[i * sizeof(uint32_t)], &v, sizeof(uint32_t));
  }
}

/*
 * Return the amount of time, in microseconds, that this took.
 */
static uint64_t time_create_random_block(char *buf, uint32_t blksize)
{
  uint64_t us_time;
  struct timeval start;
  struct timeval finish;

  gettimeofday(&start, NULL);
  create_random_block(buf, blksize);
  gettimeofday(&finish, NULL);

  {
    uint64_t istart;
    uint64_t ifinish;

    istart   = start.tv_usec;
    istart  += start.tv_sec * US_SEC;

    ifinish  = finish.tv_usec;
    ifinish += finish.tv_sec * US_SEC;

    us_time = ifinish - istart;
  }

  return us_time;
}
