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

#ifndef GAMUT_OPTS_H
#define GAMUT_OPTS_H

#include <netdb.h>      /* for uint{16,32}_t        */
#include <pthread.h>    /* for the pthread_t struct */

#include "calibrate.h"  /* for benchmark_delays     */
#include "constants.h"  /* for several #define's    */
#include "utilio.h"     /* for BUFSIZE              */
#include "workeropts.h" /* for gamut_opts         */

/************************* Begin global variables *********************/

/* Redirect stdout to a new file */
extern unsigned int redirect_stdout;

/* Will the input contain timestamps? */
extern unsigned int use_timestamps;

/* Do we run the benchmarks? */
extern unsigned int run_benchmarks;

/* Do we load benchmark data from a file? */
extern unsigned int load_benchmarks;

/* Do we save benchmark data to a file? */
extern unsigned int save_benchmarks;

/* Do we quit after running the benchmark data? */
extern unsigned int quit_benchmarks;

/* Do we debug synchronization operations? (adds some overhead) */
extern unsigned int debug_sync;

/* The log file name is global since it's needed outside this file. */
extern char log_file[];

/* The input file name is global since it's needed outside this file. */
extern char input_file[];

/************************** End global variables **********************/

/********************** Begin function declarations *******************/

/*
 * Print out a pretty usage function.
 */
extern void usage(char *progname);

/*
 * Parse command-line options and fill in the gamut_opts struct.
 * This function returns -1 on error (invalid usage), or
 *                        the number of options parsed otherwise.
 */
extern int parse_opts(int argc, char *argv[], gamut_opts *opts);

/*
 * Redirect stdout to a log file.  Upon completion, stdout will point
 *   to this new file.
 */
extern void redirect_output(void);

/*
 * Load benchmark data from a file.
 *   This function returns -1 on error, or
 *                          0 on success.
 */
extern int load_benchmark_data(void);

/*
 * Save benchmark data to a file.
 *   This function returns -1 on error, or
 *                          0 on success.
 */
extern int save_benchmark_data(void);

/*********************** End function declarations ********************/

#endif /* GAMUT_OPTS_H */
