/*
 * Copyright 2004,2005 Justin Moore, justin@cs.duke.edu
 * Copyright 2004 HP Labs, justinm@hpl.hp.com
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef GAMUT_CALIBRATE_H
#define GAMUT_CALIBRATE_H

#include <stdio.h>
#include <netdb.h>

/* Calibrate node attributes for some number of seconds each. */
#define CALIBRATE_SECONDS 1

/* What is the minimum amount of time we can sleep? */
#define MIN_SLEEP_US 15000 /* 15 ms */

/* How high can we count in one second? */
extern unsigned long long second_count;

/* How many 4-byte PRN can we generate in one second? */
extern unsigned long long prng_count;

/*
 * Calibrate this CPU to figure out how high we can count in one second.
 * We use this later on for decent CPU burn rates and exact delays for
 * sub-10ms-sleeps.
 */
extern void* calibrate_cpu(void *opt);

/*
 * Calibrate the PRNG on this particular machine.  We use it to randomize
 * the data in disk and network I/O so the system can't optimize anything.
 */
extern void* calibrate_prng(void *opt);

/*
 * Conduct all of our benchmarks.  We run the benchmarks 'num_trials'
 *   times and take the best from each one.
 */
extern void benchmark_delays(unsigned int num_trials);

/*
 * Calculate the time difference (in usec) between two timevals.
 */
extern int64_t calculate_timediff(struct timeval *a, struct timeval *b);

#endif /* GAMUT_CALIBRATE_H */
