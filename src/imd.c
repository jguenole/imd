
/******************************************************************************
*
* IMD -- The ITAP Molecular Dynamics Program
*
* Copyright 1996-2001 Institute for Theoretical and Applied Physics,
* University of Stuttgart, D-70550 Stuttgart
*
******************************************************************************/

/******************************************************************************
* $Revision$
* $Date$
******************************************************************************/

#define MAIN

/* include file also declares global variables and has function prototypes */
#include "imd.h"

/* Main module of the IMD Package;
   Various versions are built by conditional compilation */

int main(int argc, char **argv)
{
  int start;
  time_t tstart, tend;
  real tmp;
  str255 fname;
  FILE *fl;
  
  time(&tstart);

  /* reset timers */
  time_total.total = 0.0;
  time_setup.total = 0.0;
  time_main.total  = 0.0;
  time_io.total    = 0.0;
  time_force_comm.total = 0.0;
  time_force_calc.total = 0.0;

#ifdef MPI
  init_mpi(argc,argv);
#endif

  /* start some timers (after starting MPI!) */
  imd_start_timer(&time_total);
  imd_start_timer(&time_setup);

  /* read command line and parameters for first simulation phase */
  read_parameters(argc,argv);
#ifdef MPI
  broadcast_params();
#endif

  /* initialize socket I/O */
#ifdef USE_SOCKETS
  if (myid == 0) init_client();
#endif

#if !(defined(UNIAX) || defined(MONOLJ))

#if !(defined(EAM2) || defined(TERSOFF))
  /* read potential from file */
  read_pot_table1(&pair_pot,potfilename);
#endif

#ifdef TTBP
  /* read TTBP smoothing potential from file */
  read_pot_table1(&smooth_pot,ttbp_potfilename);
#endif

#ifdef TERSOFF
  init_tersoff();
#endif

#ifdef EAM2
  /* read the tabulated core-core Potential function */
  read_pot_table2(&pair_pot,eam2_core_pot_filename,ntypes*ntypes);
  /* read the tabulated embedding energy function */
  read_pot_table2(&embed_pot,eam2_emb_E_filename,ntypes);
  /* read the tabulated electron density function */
  read_pot_table2(&rho_h_tab,eam2_at_rho_filename,ntypes*ntypes);
#endif

#endif /* not UNIAX and not MONOLJ */

  /* filenames starting with _ denote internal 
     generation of the intitial configuration */
  if ('_' == infilename[0]) {
    if (0 == myid) { 
      printf("Generating atoms: %s.\n", infilename);fflush(stdout);
    }
    generate_atoms(infilename);
  }
  else {
    if (0 == myid) {
      printf("Reading atoms.\n");fflush(stdout);
    }
    init_cells();
    read_atoms(infilename);
  }
  if (0 == myid) printf("Done reading atoms.\n");

#ifdef EWALD
  init_ewald();
#endif

#ifdef OMP
  printf("\nComputing with %d thread(s).\n\n",omp_get_max_threads());
#endif

  start = steps_min;  /* keep starting step number */

  /* write .eng file header */
  if ((restart==0) && (eng_interval>0)) write_eng_file_header();

  imd_stop_timer(&time_setup);
  imd_start_timer(&time_main);

  /* first phase of the simulation */
  if (steps_min <= steps_max) main_loop();

  /* execute further phases of the simulation */
  while (finished==0) {
    simulation++;
    if (0==myid) {
      getparamfile( paramfilename, simulation );
      sprintf( itrfilename,"%s-final.itr", outfilename );
      getparamfile( itrfilename, 1 );
    }
#ifdef MPI
    broadcast_params();
#endif
    if (steps_min <= steps_max) {
      init_cells();  /* a new cell division might be necessary or useful */
      main_loop();
    }
  }

  imd_stop_timer(&time_main);
  imd_stop_timer(&time_total);

  /* write execution time summary */
  if (0 == myid) {
    time(&tend);
    steps_max -= start;

    printf("Done simulation.\n\n");
    printf("%s\n\n",progname);
    printf("started at %s", ctime(&tstart));
    printf("finished at %s\n", ctime(&tend));

#ifdef MPI
    printf("Did %d steps with %d atoms and %d CPUs.\n", 
           steps_max+1, natoms, num_cpus);
#else
    printf("Did %d steps with %d atoms.\n", steps_max+1, natoms);
#endif
    printf("Used %f seconds cputime,\n", time_total.total);
    printf("%f seconds excluding setup time,\n", time_main.total);
    tmp =  (time_main.total / (steps_max+1)) / natoms;
    printf("%.3e cpuseconds per step and atom\n", tmp);
    printf("(inverse is %.3e).\n\n", 1.0/tmp);

#ifdef TIMING
    printf("Force computation:   %e seconds or %.1f %% of main loop\n",
           time_force_calc.total,100*time_force_calc.total/time_main.total);
#ifdef MPI
    printf("Force communication: %e seconds or %.1f %% of main loop\n",
           time_force_comm.total,100*time_force_comm.total/time_main.total);
#endif
    printf("Input/Output time:   %e seconds or %.1f %% of main loop\n",
           time_io.total,100*time_io.total/time_main.total);
#endif
  
     fflush(stdout);
     fflush(stderr);
  }

  /* kill MPI */
#ifdef MPI
  shutdown_mpi();
#endif

  exit(0);

}
