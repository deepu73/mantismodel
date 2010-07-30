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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "utilio.h"

int split(char *delim, char *buf, char *args[], int maxargs,
          ws_handler spaces)
{
  char *p;
  char *empty_delim = "";
  char *my_delim;
  int nargs;

  if(!buf || !args || (maxargs < 1))
    return -1;

  if(!delim)
    my_delim = empty_delim;
  else
    my_delim = delim;

  memset(args, 0, (maxargs * sizeof(char*)));
  nargs = 0;
  p = buf;
  while(*p) {                   /* Leading delims */
    if(isspace((int) (*p))) {
      if((spaces != ws_keep) || strchr(my_delim, *p))
        *p++ = '\0';
      else
        break;
    }
    else if(strchr(my_delim, *p))
      *p++ = '\0';
    else
      break;
  }
  if(*p)
    args[nargs++] = p;
  else
    return 0;

  /*
   * Currently we're pointing at the first valid character in this
   * buffer.  Move along to an invalid character, and zero out the
   * delimiter stuff.
   */
  while(*p && (nargs < maxargs)) {
    /*
     * Move past all the valid characters 
     */
    while(*p) {
      if(isspace((int) (*p))) {
        if((spaces != ws_is_delim) && !strchr(my_delim, *p))
          p++;
        else
          break;
      }
      else if(!strchr(my_delim, *p))
        p++;
      else
        break;
    }
    if(*p == '\0')
      break;

    /*
     * If we did ws_around_delim, go backwards to find whitespace.
     */
    if(spaces == ws_around_delim) {
      char *q;

      q = p - 1;
      while(*q && (q >= buf)) {
        if(isspace((int) (*q)))
          *q-- = '\0';
        else
          break;
      }
      if(q == buf)
        return 0;
    }

    *p++ = '\0';
    while(*p) {                 /* Trailing delims */
      if(isspace((int) (*p))) {
        if((spaces != ws_keep) && !strchr(my_delim, *p))
          *p++ = '\0';
        else
          break;
      } else if(strchr(my_delim, *p))
        *p++ = '\0';
      else
        break;
    }
    if(*p == '\0')
      break;
    else
      args[nargs++] = p;
  }

  return nargs;
}

void chomp(char *buf)
{
  char *p;
  p = strrchr(buf, '\n');
  if(p)
    *p = '\0';
}

typedef enum {
  io_wait = 0,
  io_can
} io_pref;

typedef enum {
  io_read = 0,
  io_write
} io_mode;

static int io(int fd, io_mode mode, io_pref pref, uint64_t usecs)
{
  int rc;
  fd_set set;
  struct timeval tv;
  struct timeval *tvp;

  if(fd < 0)
    return 0;

  if((pref != io_can) && (pref != io_wait))
    return 0;
  if((mode != io_read) && (mode != io_write))
    return 0;

  tvp = &tv;

  while(1) {
    FD_ZERO(&set);
    FD_SET(fd, &set);

    errno = 0;
    if(pref == io_can) {
      tv.tv_sec = 0;
      tv.tv_usec = 0;
    }
    else {                     /* io_wait */
      if(!usecs) {
        tvp = NULL;
      }
      else {
        tv.tv_sec  = usecs / 1000000LL;
        tv.tv_usec = usecs % 1000000LL;
      }
    }

    if(mode == io_read)
      rc = select(fd + 1, &set, NULL, NULL, tvp);
    else                        /* io_write */
      rc = select(fd + 1, NULL, &set, NULL, tvp);

    if(!rc || (rc == 1) || (errno != EINTR))
      break;
  }

  return rc;
}

int can_read(int fd)
{
  return io(fd, io_read, io_can, (uint64_t)0);
}

int can_write(int fd)
{
  return io(fd, io_write, io_can, (uint64_t)0);
}

int wait_read(int fd, uint64_t usecs)
{
  return io(fd, io_read, io_wait, usecs);
}

int wait_write(int fd, uint64_t usecs)
{
  return io(fd, io_write, io_wait, usecs);
}

int get_line(char *buf, int len, FILE * fp, uint64_t usecs)
{
  char *p;
  int wrc;

  wrc = wait_read(fileno(fp), usecs);
  if(wrc < 0)
    return -1;
  else if(!wrc)
    return -EAGAIN;

  memset(buf, 0, len);
  p = fgets(buf, len, fp);
  if(!p && ferror(fp))
    return -1;
  else if(!p && feof(fp))
    return 0;
  else
    return strlen(buf);
}

int get_bin_line(char *buf, int len, FILE * fp, uint64_t usecs)
{
  int wrc;
  size_t rc;

  wrc = wait_read(fileno(fp), usecs);
  if(wrc < 0)
    return -1;
  else if(!wrc) /* The wait timed out */
    return -EAGAIN;

  memset(buf, 0, len);
  rc = fread(buf, 1, len, fp);
  if((rc < 0) || ferror(fp))
    return -1;
  else
    return rc;
}

int gets_line(char *buf, int len, const char *sbuf, uint64_t usecs)
{
  char *p;
  int linelen;

  if(!buf || (len < 1) || !sbuf)
    return -1;

  p = strchr(sbuf, '\n');
  if(!p) {
    linelen = strlen(sbuf);

    if(linelen > (len + 1))     /* Need one for the terminating NULL */
      return -1;

    strncpy(buf, sbuf, linelen);
    buf[linelen] = '\0';

    return linelen;
  } else {
    linelen = (int) ((unsigned int) p - (unsigned int) sbuf);

    if(linelen > (len + 1))     /* Need one for the terminating NULL */
      return -1;

    memcpy(buf, sbuf, linelen);
    buf[linelen] = '\0';

    return (linelen + 1);
  }
}

/*
 * Get the dd-style multiplier at the end of a parameter.
 */
uint64_t get_multiplier(char *mchar)
{
  uint64_t rc;

  rc = 1;
  switch(*mchar) {
    case 'k':
      rc = kilo;
      break;

    case 'K':
      rc = KILO;
      break;

    case 'm':
      rc = mega;
      break;

    case 'M':
      rc = MEGA;
      break;

    case 'g':
      rc = giga;
      break;

    case 'G':
      rc = GIGA;
      break;

    case 't':
      rc = tera;
      break;

    case 'T':
      rc = TERA;
      break;

    default:
      break;
  }

  return rc;
}

/*
 * Print an integer (i.e., 1176) as a string
 *   (i.e., "1.2 K" or ("1.1 KiB")
 */
void print_scaled_number(char *buf, uint32_t bufsize,
                         uint64_t val, int bytes)
{
  char cbytes;
  int rc;

  if(!buf || !bufsize)
    return;

  if(bytes) {
    cbytes = 'B';
  }
  else {
    cbytes = '\0';
  }

  if(bytes) {
    if(val >= TERA) {
      rc = snprintf(buf, bufsize, "%4.1f Ti%c",
                    (double)val / (double)TERA, cbytes);
    }
    else if(val >= GIGA) {
      rc = snprintf(buf, bufsize, "%4.1f Gi%c",
                    (double)val / (double)GIGA, cbytes);
    }
    else if(val >= MEGA) {
      rc = snprintf(buf, bufsize, "%4.1f Mi%c",
                    (double)val / (double)MEGA, cbytes);
    }
    else if(val >= KILO) {
      rc = snprintf(buf, bufsize, "%4.1f Ki%c",
                    (double)val / (double)KILO, cbytes);
    }
    else {
      rc = snprintf(buf, bufsize, "%4.1f %c",
                    (double)val, cbytes);
    }
  }
  else {
    if(val >= tera) {
      rc = snprintf(buf, bufsize, "%4.1f T%c",
                    (double)val / (double)tera, cbytes);
    }
    else if(val >= giga) {
      rc = snprintf(buf, bufsize, "%4.1f G%c",
                    (double)val / (double)giga, cbytes);
    }
    else if(val >= mega) {
      rc = snprintf(buf, bufsize, "%4.1f M%c",
                    (double)val / (double)mega, cbytes);
    }
    else if(val >= kilo) {
      rc = snprintf(buf, bufsize, "%4.1f K%c",
                    (double)val / (double)kilo, cbytes);
    }
    else {
      rc = snprintf(buf, bufsize, "%4.1f %c",
                    (double)val, cbytes);
    }
  }

  if(rc >= bufsize) {
    memset(buf, 0, bufsize);
  }
  else {
    buf[rc] = '\0';
  }
}
