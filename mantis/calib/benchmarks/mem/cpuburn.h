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

#ifndef GAMUT_CPUBURN_H
#define GAMUT_CPUBURN_H

#include "workeropts.h"

/*
 * Get the number of CPU burning functions defined.
 */
extern int get_num_burn_functions(void);

/*
 * Find a given function to burn CPU
 *  Calling with a NULL parameter returns the first (default) function.
 */
extern cpu_burn_func get_burn_function_by_label(char *flabel);

/*
 * Get a burn function by index.
 */
extern cpu_burn_func get_burn_function_by_index(int idx);

/*
 * Get the label by index.
 */
extern char* get_burn_label_by_index(int idx);

#endif /* GAMUT_CPUBURN_H */
