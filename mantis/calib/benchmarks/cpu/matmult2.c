// --- CPU calibration program for Mantis ---
// Author: Suzanne Rivoire (suzanne.rivoire@sonoma.edu)
// Syntax: ./matmult2 -t <params_file>
//
// Params format:
// Each line describes a matrix multiplication in the following format:
// [int/fp] <num_secs> <size> <util>
//
// Explanation:
// [int/fp]: int or fp (floating-point) -- it's a good idea to use some of both, since the power consumption can vary
// num_secs: seconds to run this particular multiplication for
// size:     size of the matrix (matrix will have size*size elements) -- good to use varying sizes to understand cache power
// util:     targeted CPU utilization (1-100)
//
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
// #include <time.h>
#include <sys/time.h>
#include <unistd.h>

#define NUM_TIMED_ITERS 50000
#define EPOCHS_PER_SEC 25
#define DEBUG 1

// --- Typedefs --------------------------------------------------

// Command-line options
typedef struct {
  char datatype[4];  // "int" or "fp"
  int  size;         // size arrays to multiply
  int  num_secs;     // Number of seconds to run
  int  util_pct;     // desired CPU utilization %; must be 0 to 100
} opts_t;

// Indices for matmult
typedef struct { int i, j, k; } indices_t;

// Set of 3 matrices (2 srcs + 1 dst)
typedef struct { 
	int       *iSrc1, *iSrc2, *iDst; 
	indices_t *prev_indices;
} i_mats_t;
typedef struct { 
	float     *fSrc1, *fSrc2, *fDst; 
	indices_t *prev_indices;
} f_mats_t;

// --- Prototypes ------------------------------------------------

// -- Verifying and executing parameters from file
int opts_ok( opts_t opts );
void exec_cmd( opts_t opts );

// -- Matrix allocation, deallocation, initialization
long mat_create_and_multiply(opts_t opts, long epochs_left) ;
i_mats_t mat_alloc_int( int size );
f_mats_t mat_alloc_float( int size );

void mat_dealloc_int( i_mats_t* mat_set );
void mat_dealloc_float( f_mats_t* mat_set );

void mat_init_int( i_mats_t* mat_set, int size );
void mat_init_float( f_mats_t* mat_set, int size );

// -- The actual multiplication
int mat_mult_int( i_mats_t* mat_set, int size, int count );
int mat_mult_float( f_mats_t* mat_set, int size, int count );

// Timing functions for sleep mode
struct timeval get_sleeptime_per_epoch(int util_pct) ;
long get_iters_per_epoch( opts_t opts );
long time_diff( struct timeval end_time, struct timeval start_time ); 

// -------------------------------------------------------------
// main()
// -------------------------------------------------------------
int main(int argc, char* argv[]) {
  opts_t opts; 
  FILE* opt_file;
  char* opt_file_name = NULL;

  // -- Check usage
  if (argc != 3) {
    fprintf(stderr, "Usage: ./matmult2 -t <param-file>");
    exit(1);
  } 
  if (strncmp(argv[1], "-t", 2) == 0) opt_file_name = argv[2];
  if (opt_file_name == NULL) { 
    fprintf(stderr, "Usage: ./matmult2 -t <param-file>");
    exit(1);
  }   

  // -- Open parameter file
  opt_file = fopen(opt_file_name, "r");
  if (opt_file == NULL) {
     fprintf(stderr, "Couldn't open parameter file for reading\n");
     exit(1);
  }

  // -- Get each line of options and run matmult
  while (fscanf( opt_file, "%s %d %d %d ", 
    opts.datatype, &(opts.size), &(opts.num_secs), &(opts.util_pct))  != EOF) {
      opts.num_secs -= 1; // subtract for timing run
    if (opts_ok(opts)) {
      printf("Performing matrix multiply for %d seconds: datatype=%s, matrix dimension=%d, utilization=%d%%.\n",
            opts.num_secs, opts.datatype, opts.size, opts.util_pct);
      exec_cmd( opts );
    }
  }
  
  // -- Close parameter file
  fclose(opt_file);
  return 0;
}

// -------------------------------------------------------------
// exec_cmd( size )
//   Execute one line of parameter file
// -------------------------------------------------------------
void exec_cmd(opts_t opts) {
  i_mats_t iMats;
  f_mats_t fMats;
  int isInt, iters_per_epoch;
  long elapsed_secs=0;
  struct timeval sleep_time, dummy_sleep_time, start_time, curr_time;

  sleep_time = get_sleeptime_per_epoch(opts.util_pct);
  isInt = (strncmp(opts.datatype, "int", strlen("int")) == 0);
  iters_per_epoch = get_iters_per_epoch( opts );

#ifdef DEBUG
  printf ("Sleeptime = %ld\n", sleep_time.tv_sec*1000000+sleep_time.tv_usec);
#endif

  if (isInt) { iMats = mat_alloc_int( opts.size );   }
  else       { fMats = mat_alloc_float( opts.size ); }

  gettimeofday(&start_time, NULL);
  do {
    //printf ("Iteration %d\n", i);
    if (isInt) {
      mat_init_int( &iMats, opts.size );
      while (!mat_mult_int( &iMats, opts.size, iters_per_epoch ) && elapsed_secs < opts.num_secs) {
         dummy_sleep_time.tv_sec = sleep_time.tv_sec;
         dummy_sleep_time.tv_usec = sleep_time.tv_usec;
         (void)select( 0, (fd_set*)NULL, (fd_set*)NULL, (fd_set *)NULL, &dummy_sleep_time );
         gettimeofday(&curr_time, NULL);
         elapsed_secs = time_diff(curr_time, start_time) / 1000000;
      }
    }
    else {
      mat_init_float( &fMats, opts.size );
      while (!mat_mult_float( &fMats, opts.size, iters_per_epoch ) && elapsed_secs < opts.num_secs) {
	dummy_sleep_time.tv_sec = sleep_time.tv_sec;
        dummy_sleep_time.tv_usec = sleep_time.tv_usec;
        (void)select( 0, (fd_set*)NULL, (fd_set*)NULL, (fd_set *)NULL, &dummy_sleep_time );
        gettimeofday(&curr_time, NULL);
        elapsed_secs = time_diff(curr_time, start_time) / 1000000;
      }
    }
    gettimeofday(&curr_time, NULL);
    elapsed_secs = time_diff(curr_time, start_time) / 1000000;
  } while (elapsed_secs < opts.num_secs);

  if (isInt) { mat_dealloc_int( &iMats );   }
  else       { mat_dealloc_float( &fMats ); }

}

// -------------------------------------------------------------
// struct timeval get_sleeptime_per_epoch(util_pct)
// Returns the amount of time/epoch to sleep based on
//    utilization percentage
// -------------------------------------------------------------
struct timeval get_sleeptime_per_epoch(int util_pct) {
  long sleep_usecs;
  struct timeval sleep_time;

  sleep_usecs = (1000000 * (100-util_pct)) / (100*EPOCHS_PER_SEC);
  sleep_time.tv_sec = sleep_usecs / 1000000;
  sleep_time.tv_usec = sleep_usecs % 1000000;
  return sleep_time;
}

// -------------------------------------------------------------
// int opts_ok(opts)
// Check options to be sure they are valid, i.e.:
//     datatype = "int" or "fp"
//     size > 0
//     100 >= util_pct > 0
//     num_secs > 0
// Return 1 if options are valid, else 0
// -------------------------------------------------------------
int opts_ok(opts_t opts) { 
  int retval = 1;
  if ((strncmp(opts.datatype, "int", 4) != 0) && (strncmp(opts.datatype, "fp", 4) != 0)) {
    fprintf(stderr, "Datatype must be \"int\" or \"fp\".\n");
    retval=0;
  }
  if (opts.size <= 0) {
    fprintf(stderr, "Matrix size must be greater than 0.\n");
    retval=0;
  }
  if ((opts.util_pct > 100) || (opts.util_pct <= 0)) {
    fprintf(stderr, "Utilization percentage must be between 1 and 100.\n");
    retval=0;
  }
  if (opts.num_secs <= 0) {
    fprintf(stderr, "Number of seconds to run must be greater than 0.\n");
    retval=0;
  }
  return retval;
}

// -------------------------------------------------------------
// i_mats_t mat_alloc_int( size )
// -------------------------------------------------------------
i_mats_t mat_alloc_int( int size ) {
  i_mats_t mat_set;
  mat_set.iSrc1 = (int *) malloc (size * size * sizeof(int));
  mat_set.iSrc2 = (int *) malloc (size * size * sizeof(int));
  mat_set.iDst  = (int *) malloc (size * size * sizeof(int));
  mat_set.prev_indices = (indices_t *) malloc (sizeof(indices_t));
  return mat_set;
}

// -------------------------------------------------------------
// f_mats_t mat_alloc_float( size )
// -------------------------------------------------------------
f_mats_t mat_alloc_float( int size ) {
  f_mats_t mat_set;
  mat_set.fSrc1 = (float *) malloc (size * size * sizeof(float));
  mat_set.fSrc2 = (float *) malloc (size * size * sizeof(float));
  mat_set.fDst  = (float *) malloc (size * size * sizeof(float));
  mat_set.prev_indices = (indices_t *) malloc (sizeof(indices_t));
  return mat_set;
}

// -------------------------------------------------------------
// mat_dealloc_int( mat_set )
// -------------------------------------------------------------
void mat_dealloc_int(i_mats_t* mat_set) {
  free(mat_set->iSrc1);
  free(mat_set->iSrc2);
  free(mat_set->iDst);
  free(mat_set->prev_indices);
}

// -------------------------------------------------------------
// mat_dealloc_float( mat_set )
// -------------------------------------------------------------
void mat_dealloc_float(f_mats_t* mat_set) {
  free(mat_set->fSrc1);
  free(mat_set->fSrc2);
  free(mat_set->fDst);
  free(mat_set->prev_indices);
}

// -------------------------------------------------------------
// mat_init_int( mat_set, size )
//   Initialize source matrices to random numbers, zero dst
// -------------------------------------------------------------
void mat_init_int(i_mats_t* mat_set, int size) {
  int i,j;

  for (i=0; i<size; i++) {
    for (j=0; j<size; j++) {
      (mat_set->iSrc1)[i*size+j] = rand();
      (mat_set->iSrc2)[i*size+j] = rand();
	  (mat_set->iDst)[i*size+j] = 0;
    }
  }
  mat_set->prev_indices->i = 0;
  mat_set->prev_indices->j = -1;
}

// -------------------------------------------------------------
// mat_init_float( mat_set, size )
//   Initialize source matrices to random numbers, zero dst
// -------------------------------------------------------------
void mat_init_float(f_mats_t* mat_set, int size){
  int i,j;
  
  for (i=0; i<size; i++) {
    for (j=0; j<size; j++) {
      (mat_set->fSrc1)[i*size+j] = (float) (rand());
      (mat_set->fSrc2)[i*size+j] = (float) (rand());
	  (mat_set->fDst)[i*size+j] = 0;
    }
  }  
  mat_set->prev_indices->i = 0;
  mat_set->prev_indices->j = -1;
}

// -------------------------------------------------------------
// int mat_mult_int( mat_set, size, count )
//   Multiply 2 matrices or run "count" iterations of j-loop
//   Return #iterations remaining if multiplication finished, 0 if "count" iters
//      ran without finishing
// -------------------------------------------------------------
int mat_mult_int( i_mats_t* mat_set, int size, int count ) {
  indices_t* ind = mat_set->prev_indices; // just for convenience
  int k;

  // Finish current i
  for ( (ind->j)=(ind->j+1); (ind->j)<size; (ind->j)++) {
    for (k=0; k<size; k++) { 
	   (mat_set->iDst)[ (ind->i)*size + (ind->j) ] += 
			(mat_set->iSrc1)[ (ind->i)*size + k ] * 
				(mat_set->iSrc2)[ k*size + (ind->j) ];  
    }
    count--;
    if (count == 0) return 0;
  }

  // Do i+1...N
  for ( (ind->i)=(ind->i)+1; (ind->i)<size; (ind->i)++) {
    for ( (ind->j)=0; (ind->j)<size; (ind->j)++) {
       for (k=0; k<size; k++) {
	     (mat_set->iDst)[ (ind->i)*size + (ind->j) ] += 
			(mat_set->iSrc1)[ (ind->i)*size + k ] * 
				(mat_set->iSrc2)[ k*size + (ind->j) ];  
       }
       count--;
       if (count == 0) return 0;
    }
  }
  return count;
}

// -------------------------------------------------------------
// int mat_mult_float( mat_set, size )
//   Multiply 2 matrices or run "count" iterations of j-loop
//   Return # iterations remaining if multiplication finished, 0 if "count" iters
//      ran without finishing
// -------------------------------------------------------------
int mat_mult_float( f_mats_t* mat_set, int size, int count ) {
  indices_t* ind = mat_set->prev_indices; // just for convenience
  int k;
  
  // Finish current i
  for ( (ind->j)=(ind->j+1); (ind->j)<size; (ind->j)++) {
    for (k=0; k<size; k++) { 
	   (mat_set->fDst)[ (ind->i)*size + (ind->j) ] += 
			(mat_set->fSrc1)[ (ind->i)*size + k ] * 
				(mat_set->fSrc2)[ k*size + (ind->j) ];  
    }
    count--;
    if (count == 0) return 0;
  }

  // Do i+1...N
  for ( (ind->i)=(ind->i)+1; (ind->i)<size; (ind->i)++) {
    for ( (ind->j)=0; (ind->j)<size; (ind->j)++) {
       for (k=0; k<size; k++) {
	     (mat_set->fDst)[ (ind->i)*size + (ind->j) ] += 
			(mat_set->fSrc1)[ (ind->i)*size + k ] * 
				(mat_set->fSrc2)[ k*size + (ind->j) ];  
       }
       count--;
       if (count == 0) return 0;
    }
  }
  return count;
}

// -------------------------------------------------------------
// long get_iters_per_epoch( opts )
// -------------------------------------------------------------
long get_iters_per_epoch( opts_t opts ) {
  i_mats_t iMats;
  f_mats_t fMats;
  int iters_left, iters_to_do = NUM_TIMED_ITERS;
  long total_usec, iters_per_epoch;
  double iters_per_usec, iters_per_epoch_d, util_pct;
  struct timeval start_time, end_time;

  // Run timing iterations
  if (strncmp(opts.datatype, "int", strlen("int")) == 0) {
    iMats = mat_alloc_int( opts.size );
    mat_init_int( &iMats, opts.size );

    // Time (num_timed_iters) iterations
    gettimeofday(&start_time, NULL);
	
    while (iters_to_do) {
	  iters_left = mat_mult_int( &iMats, opts.size, iters_to_do );
	  
	  // If we're done multiplying but not done with iterations,
	  // start over
	  if (iters_left > 0) {
		iMats.prev_indices->i = 0;
		iMats.prev_indices->j = -1;
	  }
	  iters_to_do = iters_left;
    }

    gettimeofday(&end_time, NULL);
    mat_dealloc_int( &iMats );
  }
  else {

    fMats = mat_alloc_float( opts.size );
    mat_init_float( &fMats, opts.size);
	
    // Time (num_timed_iters) iterations
    gettimeofday(&start_time, NULL);
    while (iters_to_do) {
	  iters_left = mat_mult_float( &fMats, opts.size, iters_to_do );
	  
	  // If we're done multiplying but not done with iterations,
	  // start over
	  if (iters_left > 0) {
		fMats.prev_indices->i = 0;
		fMats.prev_indices->j = -1;
	  }
	  iters_to_do = iters_left;
    }
	
    gettimeofday(&end_time, NULL);
    mat_dealloc_float( &fMats );
  }

//   printf("Start time: (%ld, %d). End time: (%ld, %d)\n", (start_time.tv_sec), start_time.tv_usec, end_time.tv_sec, end_time.tv_usec);

  // Compute iterations/epoch = iterations/sec * sec/epoch * util_pct/100
  // = num_timed_iters / (total_time * epochs_per_sec) * util_pct/100
  total_usec = time_diff( end_time, start_time );
  
#ifdef DEBUG
  printf("%ld usecs for all timed iterations\n", total_usec);
#endif

  iters_per_usec  = ((double)NUM_TIMED_ITERS) / ((double)total_usec);
  util_pct = ((double)opts.util_pct)/100.0;
  iters_per_epoch_d = iters_per_usec * util_pct * 1000000.0 / EPOCHS_PER_SEC;
  iters_per_epoch = (long)iters_per_epoch_d;

#ifdef DEBUG
  printf ("iters per usec =%f\n", iters_per_usec);
  printf ("Iters per epoch = %ld\n", iters_per_epoch);
#endif
  
  return iters_per_epoch;
}

// -------------------------------------------------------------
// long time_diff( end_time, start_time )
//   return (end_time - start_time) in usec
// -------------------------------------------------------------
long time_diff( struct timeval end_time, struct timeval start_time ) {
  long start_time_usecs, end_time_usecs, diff_time_usecs;

  start_time_usecs = start_time.tv_sec * 1000000 + start_time.tv_usec;
  end_time_usecs   = end_time.tv_sec * 1000000 + end_time.tv_usec;

  diff_time_usecs = end_time_usecs - start_time_usecs;
  return diff_time_usecs;
}
