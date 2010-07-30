/*
 * Copyright 2005 Justin Moore, justin@cs.duke.edu
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _UTIL_LOG_H
#define _UTIL_LOG_H

/*
 * Essentially ripped out of syslog(3) man page.
 */
typedef enum {
     G_EMERG = 0,  /* Fatal error.  Exit now */
     G_ERR,        /* Unrecoverable error.  Exit after cleanup. */
     G_WARNING,    /* Recoverable error.  Print and continue. */
     G_NOTICE,     /* Standard debug level. */
     G_INFO,       /* More verbose information. */
     G_DEBUG,      /* Nit-picky details on normal operation. */
     G_DSYNC,      /* Include synchronization operations in log. */
     G_DLOOP,      /* Debugging data in tight loops. */
     G_MAX_DEBUG   /* Symbolic placeholder */
     } s_log_level;

extern void set_log_level(s_log_level level);
extern void set_log_stream(FILE *fp);
extern s_log_level get_log_level(void);
extern void set_log_label(char *label);
extern int s_log(s_log_level level, char *format, ...)
                 __attribute__ ((format (printf, 2, 3)));

#endif /* _UTIL_LOG_H */
