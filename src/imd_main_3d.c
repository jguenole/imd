
/******************************************************************************
*
* IMD -- The ITAP Molecular Dynamics Program
*
* Copyright 1996-2004 Institute for Theoretical and Applied Physics,
* University of Stuttgart, D-70550 Stuttgart
*
******************************************************************************/

/******************************************************************************
*
* imd_main_3d.c -- main loop, used for both two and three dimensions
*
******************************************************************************/

/******************************************************************************
* $Revision$
* $Date$
******************************************************************************/

#include "imd.h"

/*****************************************************************************
*
*  main_loop
*
*****************************************************************************/

void main_loop(void)
{
  real tmp_pot_energy, tmp_kin_energy;
  int  i, j, k, l;
  int  steps_diff = steps_max - steps_min;
  int  deform_int = 0, fbc_int = 0, do_fbc_incr = 0, is_relaxed = 0;
  int  have_fbc_incr = 0;
  real dtemp, dshock_speed, old_epot = 0.0;
  vektor d_pressure, *fbc_df;
  real tmpvec1[DIM], tmpvec2[DIM];
#ifdef TWOD
  vektor nullv={0.0,0.0};
#else
  vektor nullv={0.0,0.0,0.0};
#endif
#if defined(CORRELATE) || defined(MSQD)
  int ref_step = correl_start;
#endif

#ifdef FBC
  fbc_df = (vektor *) malloc(vtypes*DIM*sizeof(real));
  if (NULL==fbc_df) error("Cannot allocate memory for fbc_df");
#endif

#ifdef SHOCK
  /* compute speed increase */
  if (shock_mode == 3) { 
    dshock_speed=0;
    if (shock_incr>0) {
      dshock_speed=shock_speed/(real)shock_incr;
      shock_speed=0.0;
    }
  }
#endif

  if (0==imdrestart) {
    /* initialize temperature, if necessary */
    if (do_maxwell) maxwell(temperature);
    do_maxwell=0;
  }

#if defined(FRAC) || defined(FTG) 
  if (0==myid) {
      printf( "Strain rate is  %1.10f\n", dotepsilon0 );
      printf( "Damping mode  %d\n", dampingmode );
      printf( "Damping prefactor is %1.10f\n\n", gamma_bar );
  }
#endif

  if (0==myid) printf( "Starting simulation %d\n", simulation );

#if defined(AND) || defined(NVT) || defined(NPT) || defined(STM) || defined(FRAC)
  dtemp = (end_temp - temperature) / steps_diff;
#endif

#ifdef FBC
#ifdef RELAX
  for (l=0; l<vtypes; l++) {
    if ((fbc_dforces+l)->x != 0.0) have_fbc_incr = 1;
    if ((fbc_dforces+l)->y != 0.0) have_fbc_incr = 1;
#ifndef TWOD
    if ((fbc_dforces+l)->z != 0.0) have_fbc_incr = 1;
#endif
  }
#else
  /* dynamic loading, increment linearly at each timestep */
  if ((ensemble!=ENS_MIK) && (ensemble!=ENS_GLOK) && (ensemble!=ENS_CG)) {
    for (l=0;l<vtypes;l++){
      (fbc_df+l)->x = ((fbc_endforces+l)->x-(fbc_beginforces+l)->x)/steps_diff;
      (fbc_df+l)->y = ((fbc_endforces+l)->y-(fbc_beginforces+l)->y)/steps_diff;
#ifndef TWOD
      (fbc_df+l)->z = ((fbc_endforces+l)->z-(fbc_beginforces+l)->z)/steps_diff;
#endif
    }
    do_fbc_incr = 1;
  }
#endif /* RELAX */
#endif /* FBC */

#ifdef NVX
  dtemp = (dTemp_end - dTemp_start) / steps_diff;
  tran_Tleft  = temperature + dTemp_start;
  tran_Tright = temperature - dTemp_start;
#endif

#ifdef NPT
  d_pressure.x = (pressure_end.x - pressure_ext.x) / steps_diff;
  d_pressure.y = (pressure_end.y - pressure_ext.y) / steps_diff;
#ifndef TWOD
  d_pressure.z = (pressure_end.z - pressure_ext.z) / steps_diff;
#endif
  calc_dyn_pressure();
  if (isq_tau_xi==0.0) xi = nullv;
#endif

#if defined(CORRELATE) || defined(MSQD)
  init_correl(ncorr_rmax,ncorr_tmax);
#endif

#ifdef ATDIST
  if (atdist_int > 0) init_atdist();
#endif

#ifdef DIFFPAT
  if (diffpat_int > 0) init_diffpat();
#endif

#ifdef NBLIST
  make_nblist();
#endif

#ifdef CG
  if (ensemble == ENS_CG) reset_cg();
#endif

  /* simulation loop */
  for (steps=steps_min; steps <= steps_max; ++steps) {

#ifdef SHOCK
    /* accelerate blocks */
    if (shock_mode == 3 && shock_incr>0) { 
      if (steps<=shock_incr){  
	  shock_speed+=dshock_speed;
	  for (k=0; k<ncells; ++k) {
	      cell *p;
	      p = cell_array + CELLS(k);
	      for (i=0; i<p->n; ++i) {
		  IMPULS(p,i,X) += dshock_speed * MASSE(p,i);
	      }
	  }
      }
    }
#endif

#ifdef STRESS_TENS
    do_press_calc = (((eng_int  > 0) && (0 == steps % eng_int )) ||
                     ((dist_int > 0) && (0 == steps % dist_int)) ||
                     (relax_rate > 0.0) );
#endif

#ifdef EPITAX
    for (i=0; i<ntypes; ++i ) {
      if ( (steps >= epitax_startstep) && (steps <= epitax_maxsteps) ) {  
	if ( (epitax_rate[i] != 0) && ((steps-steps_min)%epitax_rate[i])==0 ) {
	  delete_atoms(); 
	  create_atom(i, epitax_mass[i], epitax_temp[i]);
	}
	else if ( (epitax_rate[i] != 0) && (steps > epitax_maxsteps) && 
                  ( (steps-steps_min)%epitax_rate[i]) == 0 ) 
	  delete_atoms();
      }
    }
#endif

#if defined(FBC) && defined(RELAX)
    /* set fbc increment if necessary */
    if ((ensemble==ENS_MIK) || (ensemble==ENS_GLOK) || (ensemble==ENS_CG)) {
      if ((is_relaxed) || (fbc_int > max_fbc_int)) { 
        for (l=0; l<vtypes; l++) fbc_df[l] = fbc_dforces[l];
        fbc_int = 0;
        do_fbc_incr = 1;
      }
      else {
        for (l=0; l<vtypes; l++) *(fbc_df+l) = nullv;
        do_fbc_incr = 0;
        fbc_int++;
      }
    }
#endif

#ifdef HOMDEF
    if ((exp_interval > 0) && (0 == steps % exp_interval)) expand_sample();
    if ((hom_interval > 0) && (0 == steps % hom_interval)) shear_sample();
    if ((lindef_interval > 0) && (0 == steps % lindef_interval)) 
#ifdef TWOD
      lin_deform(lindef_x, lindef_y,           lindef_size);
#else
      lin_deform(lindef_x, lindef_y, lindef_z, lindef_size);
#endif
#endif

#ifdef DEFORM
    if (max_deform_int > 0) {
      if ((is_relaxed) || (deform_int == max_deform_int)) {
        deform_sample();
        deform_int=0;
#ifdef CG
        if (ensemble == ENS_CG) reset_cg();
#endif
      }
      deform_int++;
    }
#endif

#ifdef AVPOS
    if ((steps == steps_min) || (steps == avpos_start)) {
       update_avpos();
    }
#endif
#ifdef ATDIST
    if ((atdist_int > 0) && (steps >= atdist_start) && 
        (steps <= atdist_end)) update_atdist();
#endif
#ifdef DIFFPAT
    if ((diffpat_int > 0) && (steps >= diffpat_start) && 
        (steps <= diffpat_end)) update_diffpat(steps);
#endif

#ifdef FBC
    /* apply fbc increment if necessary */
    if (do_fbc_incr == 1) {
      for (l=0; l<vtypes; l++){ 
        (fbc_forces+l)->x += (fbc_df+l)->x;  
        (fbc_forces+l)->y += (fbc_df+l)->y;
#ifndef TWOD
        (fbc_forces+l)->z += (fbc_df+l)->z;
#endif
      } 
#ifdef CG
      if (ensemble == ENS_CG) reset_cg();
#endif
    }
#endif

#ifdef CG
    if (ensemble == ENS_CG) cg_step(steps);
    else
#endif

    calc_forces(steps);

#ifdef RIGID
    /* total force on superparticles (for each cpu) */
    for(k=0; k<ncells; k++) {
      cell *p;
      int sorte;
      p = CELLPTR(k);
      for(i=0; i<p->n; i++) {
	sorte = VSORTE(p,i);
	if ( superatom[sorte] > -1 ) {
	  superforce[superatom[sorte]].x += KRAFT(p,i,X);
	  superforce[superatom[sorte]].y += KRAFT(p,i,Y);
#ifndef TWOD
	  superforce[superatom[sorte]].z += KRAFT(p,i,Z);
#endif

#ifdef FBC
	 superforce[superatom[sorte]].x += (fbc_forces+sorte)->x;
	 superforce[superatom[sorte]].y += (fbc_forces+sorte)->y;
#ifndef TWOD
	 superforce[superatom[sorte]].z += (fbc_forces+sorte)->z;
#endif
#endif
	}
      }
    }
#ifdef MPI
    /* total force on superparticles */
    for(i=0; i<nsuperatoms; i++) {
      tmpvec1[0] = superforce[i].x;
      tmpvec1[1] = superforce[i].y;
#ifndef TWOD
      tmpvec1[2] = superforce[i].z;
#endif
      MPI_Allreduce( tmpvec1, tmpvec2, DIM, REAL, MPI_SUM, cpugrid); 

      superforce[i].x = tmpvec2[0];
      superforce[i].y = tmpvec2[1];
#ifndef TWOD
      superforce[i].z = tmpvec2[2];
#endif
    }
#endif
#endif

#ifdef FORCE
    /* we have to write the forces *before* the atoms are moved */
    if ((force_int > 0) && (0 == steps % force_int)) 
       write_config_select( steps/force_int, "force",
                            write_atoms_force, write_header_force);
#endif

#ifdef WRITEF /* can be used as tool for postprocessing */
    write_config_select(steps, "wf",write_atoms_wf, write_header_wf);
#endif

#ifdef EPITAX
    if (steps == steps_min) {
      calc_poteng_min();
      if (0 == myid) 
	printf("EPITAX: Minimal potential energy = %lf\n", epitax_poteng_min);
    }
#endif

#ifdef DISLOC
    if ((steps==reset_Epot_step) && (calc_Epot_ref==1)) reset_Epot_ref();
#endif

#if defined(CORRELATE) || defined(MSQD)
    if ((steps >= correl_start) && ((steps < correl_end) || (correl_end==0))) {
      int istep = steps - correl_start;
      if (istep % correl_ts == 0) correlate(steps,ref_step,istep/correl_ts);
      if ((correl_int != 0) && (steps-ref_step+1 >= correl_int)) 
        ref_step += correl_int;
    }
#endif

#ifdef GLOK 
    /* "global convergence": set momenta to 0 if P*F < 0 (global vectors) */
    if (ensemble == ENS_GLOK) {
      real ekin = 2 * tot_kin_energy / nactive;
      if ((PxF < 0) || (ekin > glok_ekin_threshold) || (steps == steps_min)) {
        for (k=0; k<NCELLS; ++k) {
          cell *p;
          p = CELLPTR(k);
          for (i=0; i<p->n; ++i) {
            IMPULS(p,i,X) = 0.0;
            IMPULS(p,i,Y) = 0.0;
#ifndef TWOD
            IMPULS(p,i,Z) = 0.0;
#endif
          }
        }
        write_eng_file(steps); 
      }
    }
#endif

    if (ensemble != ENS_CG) move_atoms(); /* here PxF is recalculated */

#ifdef EPITAX
    /* beam atoms are always integrated by NVE */
    if (ensemble != ENS_NVE) move_atoms_nve();
#endif

#if defined(AND) || defined(NVT) || defined(NPT) || defined(STM) || defined(FRAC)
    if ((steps==steps_min) && (use_curr_temp==1)) {
#ifdef UNIAX
      temperature = 2.0 * tot_kin_energy / (nactive + nactive_rot);
#else
      temperature = 2.0 * tot_kin_energy / nactive;
#endif
      dtemp = (end_temp - temperature) / steps_diff;
      use_curr_temp = 0;
    }
#endif

#ifdef NPT_iso
    if ((steps==steps_min) && (ensemble==ENS_NPT_ISO) && 
        (use_curr_pressure==1)) {
      pressure_ext.x = pressure;
      d_pressure.x = (pressure_end.x-pressure_ext.x) / steps_diff;
      d_pressure.y = (pressure_end.y-pressure_ext.y) / steps_diff;
#ifndef TWOD
      d_pressure.z = (pressure_end.z-pressure_ext.z) / steps_diff;
#endif
      use_curr_pressure = 0;
    }
#endif

#ifdef NPT_axial
    if ((steps==steps_min) && (ensemble==ENS_NPT_AXIAL) && 
        (use_curr_pressure==1)) {
      pressure_ext.x = stress_x;
      pressure_ext.y = stress_y;
#ifndef TWOD
      pressure_ext.z = stress_z;
#endif
      d_pressure.x = (pressure_end.x-pressure_ext.x) / steps_diff;
      d_pressure.y = (pressure_end.y-pressure_ext.y) / steps_diff;
#ifndef TWOD
      d_pressure.z = (pressure_end.z-pressure_ext.z) / steps_diff;
#endif
      use_curr_pressure = 0;
    }
#endif

#if defined(AND) || defined(NVT) || defined(NPT) || defined(STM) || defined(FRAC)
    temperature += dtemp;
#endif

#ifdef NVX
    tran_Tleft   += dtemp;
    tran_Tright  -= dtemp;
#endif


#ifdef NPT
    pressure_ext.x += d_pressure.x;
    pressure_ext.y += d_pressure.y;
#ifndef TWOD
    pressure_ext.z += d_pressure.z;
#endif
#endif

    /* Periodic I/O */
#ifdef TIMING
    imd_start_timer(&time_io);
#endif
    if ((checkpt_int > 0) && (0 == steps % checkpt_int)) 
       write_config( steps/checkpt_int, steps);
    if ((eng_int  > 0) && (0 == steps % eng_int )) write_eng_file(steps);
    if ((dist_int > 0) && (0 == steps % dist_int)) write_distrib(steps);
    if ((pic_int  > 0) && (0 == steps % pic_int )) write_pictures(steps);

#ifdef EFILTER  /* just print atoms if in an energy-window */ 
    if ((ef_checkpt_int > 0) && (0 == steps % ef_checkpt_int)) 
       write_config_select( steps/ef_checkpt_int, "ef",
                            write_atoms_ef, write_header_ef);
#endif
#ifdef NBFILTER  /* just print atoms by neighbour condition */ 
    if ((nb_checkpt_int > 0) && (0 == steps % nb_checkpt_int)) 
       write_config_select( steps/nb_checkpt_int, "nb",
                            write_atoms_nb, write_header_nb);
#endif
#ifdef ATDIST
    if ((atdist_pos_int > 0) && (0 == steps % atdist_pos_int))
       write_config_select( steps / atdist_pos_int, "cpt", 
                            write_atoms_atdist_pos, write_header_atdist_pos);
#endif
#ifdef DISLOC
    if (steps == up_ort_ref) update_ort_ref();
    if ((dem_int > 0) && (0 == steps % dem_int)) 
       write_config_select(steps, "dem", write_atoms_dem, write_header_dem);
    if ((dsp_int > up_ort_ref) && (0 == steps % dsp_int)) 
       write_config_select(steps, "dsp", write_atoms_dsp, write_header_dsp);
#endif
#ifdef AVPOS
    if ( steps <= avpos_end ){
      if ((avpos_res > 0) && (0 == (steps - avpos_start) % avpos_res) && 
          (steps > avpos_start)) add_positions();
      if ((avpos_int > 0) && (0 == (steps - avpos_start) % avpos_int) && 
          (steps > avpos_start)) {
        write_config_select((steps - avpos_start) / avpos_int,"avp",
                            write_atoms_avp,write_header_avp);
        write_avpos_itr_file((steps - avpos_start) / avpos_int, steps);
        update_avpos();
      }
    }
#endif
#ifdef TRANSPORT 
    if ((tran_interval > 0) && (steps > 0) && (0 == steps%tran_interval)) 
       write_temp_dist(steps);
#endif
#ifdef RNEMD
    if ((exch_interval > 0) && (0 == steps%exch_interval)) 
       rnemd_heat_exchange();
#endif

#ifdef STRESS_TENS
    if ((press_int > 0) && (0 == steps % press_int)) {
       write_config_select( steps/press_int, "press",
                            write_atoms_press, write_header_press);
    }
#endif

#ifdef USE_SOCKETS
    if ((socket_int > 0) && (0 == steps % socket_int)) check_socket();
#endif

#ifdef TIMING
    imd_stop_timer(&time_io);
#endif

#ifdef HOMDEF
    if (relax_rate > 0.0) relax_pressure();
#endif

#ifdef RELAX
    if ((ensemble==ENS_MIK) || (ensemble==ENS_GLOK) || (ensemble==ENS_CG)) {

      int stop = 0;
      real fnorm2 = SQRT( fnorm / nactive );
      real ekin   = 2 * tot_kin_energy / nactive;
      real epot   = tot_pot_energy / natoms;
      real delta_epot = old_epot - epot;
      if (delta_epot < 0) delta_epot = -delta_epot;

      if ((ekin  <  ekin_threshold) || (fnorm2 < fnorm_threshold) || 
          (f_max < f_max_threshold) || (delta_epot < delta_epot_threshold)) 
        is_relaxed = 1;
      old_epot = epot;

      if (is_relaxed) {
        stop = 1;
        write_eng_file(steps);
        write_ssconfig(steps);
        if (myid==0) {
          printf("nfc = %d epot = %22.16f\n", nfc, epot );
          printf("ekin = %e fnorm = %e f_max = %e delta_epot = %e\n", 
                 ekin, fnorm2, f_max, delta_epot);
        }
      }

#ifdef DEFORM
      if (max_deform_int > 0) stop=0;
#endif
#ifdef FBC
      if (have_fbc_incr) stop=0;
#endif
      if (stop) steps_max = steps;
    }
#endif

#ifdef NBLIST
    check_nblist();
#else
    do_boundaries();    
    fix_cells();  
#endif

#ifdef ATDIST
    if ((atdist_int > 0) && (steps == atdist_end)) write_atdist();
#endif
#ifdef DIFFPAT
    if ((diffpat_int > 0) && (steps == diffpat_end)) write_diffpat();
#endif
#ifdef MSQD
    if ((correl_end >0) && (steps==correl_end) || 
        (correl_end==0) && (steps==steps_max))   
      write_config_select(0, "sqd", write_atoms_sqd, write_header_sqd);
#endif

    /* finish, if stop file is found */
    if ((stop_int > 0) && (0==steps%stop_int)) {
      int stop = 0;
      if (0 == myid) {
        FILE *testfile = fopen("stop","r");
        if (NULL!=testfile) { 
          fclose(testfile);
          unlink("stop");
          stop = 1;
	}
      }
#ifdef MPI
      MPI_Bcast( &stop, 1, MPI_INT, 0, MPI_COMM_WORLD);
#endif
      if (stop) {
        if (myid == 0) printf("Stop file found after %d steps\n", steps);
        write_config(-1,steps);
        steps_max = steps;
        finished = 1;
        break;
      }
    }

    /* finish, if maxwalltime is reached */
    if (maxwalltime > 0) {
      double tdiff = difftime(time(&tend), tstart);
#ifdef MPI
      MPI_Bcast( &tdiff, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
#endif
      if (tdiff > maxwalltime) {
        if (myid == 0) 
          printf("Maximal allowed walltime reached after %d steps\n", steps);
        write_config(-1,steps);
        steps_max = steps;
        finished = 1;
        break;
      }
    }
  }

  /* clean up the current phase, and clear restart flag */
  imdrestart=0;
  if (0==myid) {
    write_itr_file(-1, steps_max,"");
    printf( "End of simulation %d\n", simulation );
  }  
}


/******************************************************************************
*
* do_boundaries
*
* Apply periodic boundaries to all atoms
* Could change so that only cells on surface to some work
*
******************************************************************************/

void do_boundaries(void)
{
  int k;

  /* for each cell in bulk */
#ifdef _OPENMP
#pragma omp parallel for
#endif
  for (k=0; k<NCELLS; ++k) {

    int  l;
    real i;  /* FLOOR returns a real */
    cell *p;

    p = CELLPTR(k);

    /* PBC in x direction */
    if (pbc_dirs.x==1)
    for (l=0; l<p->n; ++l) {
      i = -FLOOR( SPRODX( &ORT(p,l,X), tbox_x) );
      ORT(p,l,X)     += i * box_x.x;
      ORT(p,l,Y)     += i * box_x.y;
#ifndef TWOD
      ORT(p,l,Z)     += i * box_x.z;
#endif
#ifdef MSQD
      REF_POS(p,l,X) += i * box_x.x;
      REF_POS(p,l,Y) += i * box_x.y;
#ifndef TWOD
      REF_POS(p,l,Z) += i * box_x.z;
#endif
#endif
#ifdef AVPOS
      SHEET(p,l,X)   -= i * box_x.x;
      SHEET(p,l,Y)   -= i * box_x.y;
#ifndef TWOD
      SHEET(p,l,Z)   -= i * box_x.z;
#endif
#endif
    }

    /* PBC in y direction */
    if (pbc_dirs.y==1)
    for (l=0; l<p->n; ++l) {
      i = -FLOOR( SPRODX( &ORT(p,l,X), tbox_y) );
      ORT(p,l,X)     += i * box_y.x;
      ORT(p,l,Y)     += i * box_y.y;
#ifndef TWOD
      ORT(p,l,Z)     += i * box_y.z;
#endif
#ifdef MSQD
      REF_POS(p,l,X) += i * box_y.x;
      REF_POS(p,l,Y) += i * box_y.y;
#ifndef TWOD
      REF_POS(p,l,Z) += i * box_y.z;
#endif
#endif
#ifdef AVPOS
      SHEET(p,l,X)   -= i * box_y.x;
      SHEET(p,l,Y)   -= i * box_y.y;
#ifndef TWOD
      SHEET(p,l,Z)   -= i * box_y.z;
#endif
#endif
    }

#ifndef TWOD
    /* PBC in z direction */
    if (pbc_dirs.z==1)
    for (l=0; l<p->n; ++l) {
      i = -FLOOR( SPRODX( &ORT(p,l,X), tbox_z) );
      ORT(p,l,X)     += i * box_z.x;
      ORT(p,l,Y)     += i * box_z.y;
      ORT(p,l,Z)     += i * box_z.z;
#ifdef MSQD
      REF_POS(p,l,X) += i * box_z.x;
      REF_POS(p,l,Y) += i * box_z.y;
      REF_POS(p,l,Z) += i * box_z.z;
#endif
#ifdef AVPOS
      SHEET(p,l,X)   -= i * box_z.x;
      SHEET(p,l,Y)   -= i * box_z.y;
      SHEET(p,l,Z)   -= i * box_z.z;
#endif
    }
#endif
  }
}

#ifdef STRESS_TENS

/******************************************************************************
*
*  calc_tot_presstens
*
******************************************************************************/

void calc_tot_presstens(void)
{ 
  int i;

  real tmp_presstens1[6], tmp_presstens2[6];

  tot_presstens.xx = 0.0; 
  tot_presstens.yy = 0.0; 
  tot_presstens.xy = 0.0;
#ifndef TWOD
  tot_presstens.zz = 0.0; 
  tot_presstens.yz = 0.0;
  tot_presstens.zx = 0.0;
#endif

  /* sum up total pressure tensor */
  for (i=0; i<NCELLS; ++i) {
    int j;
    cell *p;
    p = CELLPTR(i);
    for (j=0; j<p->n; ++j) {
      tot_presstens.xx += PRESSTENS(p,j,xx);
      tot_presstens.yy += PRESSTENS(p,j,yy);
      tot_presstens.xy += PRESSTENS(p,j,xy);  
#ifndef TWOD
      tot_presstens.zz += PRESSTENS(p,j,zz);
      tot_presstens.yz += PRESSTENS(p,j,yz);  
      tot_presstens.zx += PRESSTENS(p,j,zx);  
#endif
    }
  }

#ifdef MPI

  tmp_presstens1[0] = tot_presstens.xx; 
  tmp_presstens1[1] = tot_presstens.yy; 
  tmp_presstens1[2] = tot_presstens.xy;
#ifndef TWOD
  tmp_presstens1[3] = tot_presstens.zz; 
  tmp_presstens1[4] = tot_presstens.yz;
  tmp_presstens1[5] = tot_presstens.zx;
#endif

#ifdef TWOD
  MPI_Allreduce( tmp_presstens1, tmp_presstens2, 3, REAL, MPI_SUM, cpugrid);
#else
  MPI_Allreduce( tmp_presstens1, tmp_presstens2, 6, REAL, MPI_SUM, cpugrid);
#endif

  tot_presstens.xx  = tmp_presstens2[0];
  tot_presstens.yy  = tmp_presstens2[1]; 
  tot_presstens.xy  = tmp_presstens2[2]; 
#ifndef TWOD
  tot_presstens.zz  = tmp_presstens2[3];
  tot_presstens.yz  = tmp_presstens2[4]; 
  tot_presstens.zx  = tmp_presstens2[5]; 
#endif

#endif /* MPI */

}

#endif/* STRESS_TENS */

