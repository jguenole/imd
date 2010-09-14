
/******************************************************************************
*
* IMD -- The ITAP Molecular Dynamics Program
*
* Copyright 1996-2007 Institute for Theoretical and Applied Physics,
* University of Stuttgart, D-70550 Stuttgart
*
******************************************************************************/

/******************************************************************************
*
* imd_neb -- functions for the NEB method
*
******************************************************************************/

/******************************************************************************
* $Revision$
* $Date$
******************************************************************************/

#include "imd.h"

#ifdef TWOD
#define nebSPRODN(x,y) ( (x)[0]*(y)[0] + (x)[1]*(y)[1] )
#else
#define nebSPRODN(x,y) ( (x)[0]*(y)[0] + (x)[1]*(y)[1] + (x)[2]*(y)[2] )
#endif

/* auxiliary arrays */
real *pos=NULL, *pos_l=NULL, *pos_r=NULL, *f=NULL;

/******************************************************************************
*
*  initialize MPI (NEB version)
*
******************************************************************************/

void init_mpi(void)
{
  /* Initialize MPI */
  MPI_Comm_size(MPI_COMM_WORLD,&num_cpus);
  MPI_Comm_rank(MPI_COMM_WORLD,&myrank);
  if (0 == myrank) { 
    printf("NEB: Starting up MPI with %d processes.\n", num_cpus);
  }
}

/******************************************************************************
*
*  shutdown MPI (NEB version)
*
******************************************************************************/

void shutdown_mpi(void)
{
  MPI_Barrier(MPI_COMM_WORLD);   /* Wait for all processes to arrive */
#ifdef MPELOG
  MPE_Log_sync_clocks();
#ifdef NO_LLMPE
  MPE_Finish_log( progname );
#endif
#endif
  MPI_Finalize();                /* Shutdown */
}

/******************************************************************************
*
*  allocate auxiliary arrays
*
******************************************************************************/

void alloc_pos(void) 
{
  pos   = (real *) malloc( DIM * natoms * sizeof(real ) );
  pos_l = (real *) malloc( DIM * natoms * sizeof(real ) );
  pos_r = (real *) malloc( DIM * natoms * sizeof(real ) );
  f     = (real *) malloc( DIM * natoms * sizeof(real ) );
  if ((NULL==pos) || (NULL==pos_l) || (NULL==pos_r) || (NULL==f))
    error("cannot allocate NEB position arrays");
}

/******************************************************************************
*
*  read all configurations (including initial and final)
*
******************************************************************************/

void read_atoms_neb(str255 infilename)
{
  str255 fname;
  int i, k, n;

  /* keep a copy of the outfile name without replica suffix */
  neb_outfilename = strdup(outfilename);

  /* read positions of initial configuration */
  if (0==myrank) {
    sprintf(fname, "%s.%02d", infilename, 0);
    myrank = 1;  /* avoid double info messages */
    read_atoms(fname);
    myrank = 0;
    alloc_pos();
    for (k=0; k<NCELLS; k++) {
      cell *p = CELLPTR(k);
      for (i=0; i<p->n; i++) { 
        n = NUMMER(p,i);
        pos_l X(n) = ORT(p,i,X);
        pos_l Y(n) = ORT(p,i,Y);
        pos_l Z(n) = ORT(p,i,Z);
      }
    }
    /* compute and write energy of initial configuration */
    calc_forces(0);
    neb_image_energies[0]=tot_pot_energy;
    sprintf(outfilename, "%s.%02d", neb_outfilename, 0);
    write_eng_file_header();
    write_eng_file(0);
    fclose(eng_file);
    eng_file = NULL;
  }

  /* read positions of final configuration */
  if (neb_nrep-3==myrank) {
    sprintf(fname, "%s.%02d", infilename, neb_nrep-1);
    read_atoms(fname);
    if (NULL==pos) alloc_pos();
    for (k=0; k<NCELLS; k++) {
      cell *p = CELLPTR(k);
      for (i=0; i<p->n; i++) { 
        n = NUMMER(p,i);
        pos_r X(n) = ORT(p,i,X);
        pos_r Y(n) = ORT(p,i,Y);
        pos_r Z(n) = ORT(p,i,Z);
      }
    }
    /* compute and write energy of initial configuration */
    calc_forces(0);
    neb_image_energies[ neb_nrep-1]=tot_pot_energy;
    sprintf(outfilename, "%s.%02d", neb_outfilename, neb_nrep-1);
    write_eng_file_header();
    write_eng_file(0);
    fclose(eng_file);
    eng_file = NULL;
  }

  /* read positions of my configuration */
  sprintf(fname, "%s.%02d", infilename, myrank+1);
  read_atoms(fname);
  if (NULL==pos) alloc_pos();
  sprintf(outfilename, "%s.%02d", neb_outfilename, myrank+1);

}

/******************************************************************************
*
*  exchange positions with neighbor replicas
*
******************************************************************************/

void neb_sendrecv_pos(void)
{
  int i, k, n, cpu_l, cpu_r;
  MPI_Status status;

  /* fill pos array */
  for (k=0; k<NCELLS; k++) {
    cell *p = CELLPTR(k);
    for (i=0; i<p->n; i++) { 
      n = NUMMER(p,i);
      pos X(n) = ORT(p,i,X);
      pos Y(n) = ORT(p,i,Y);
      pos Z(n) = ORT(p,i,Z);
    }
  }

  /* ranks of left/right cpus */
  cpu_l = (0            == myrank) ? MPI_PROC_NULL : myrank - 1;
  cpu_r = (neb_nrep - 3 == myrank) ? MPI_PROC_NULL : myrank + 1;

  /* send positions to right, receive from left */
  MPI_Sendrecv(pos,   DIM*natoms, REAL, cpu_r, BUFFER_TAG,
	       pos_l, DIM*natoms, REAL, cpu_l, BUFFER_TAG,
	       MPI_COMM_WORLD, &status );

  /* send positions to left, receive from right */
  MPI_Sendrecv(pos,   DIM*natoms, REAL, cpu_l, BUFFER_TAG,
	       pos_r, DIM*natoms, REAL, cpu_r, BUFFER_TAG,
	       MPI_COMM_WORLD, &status );
}

/******************************************************************************
*
*  modify forces according to NEB
*
******************************************************************************/

void calc_forces_neb(void)
{
  real dl2=0.0, dr2=0.0, drl=0.0, d2=0.0, f2=0.0, f2max=0.0, drlmax=0.0;
  real tmp, cosphi, fphi, src[3], dest[3], *d=pos;
  int k, i;
 
  int myimage,maximage;
  real V_previous, V_actual, V_next;
  real deltaVmin,deltaVmax;
  real normdr,normdl,inormd;
  real Eref,Emax,Emin,delta_E;
  real ratio_plus,ratio_minus,abs_next,abs_previous ;
  real k_sum, k_diff;
  real tmp_neb_ks[NEB_MAXNREP] INIT(zero100);

  myimage = myrank+1;
  /* get info about the energies of the different images */
  neb_image_energies[ myimage]=tot_pot_energy;
  MPI_Allreduce(neb_image_energies , neb_epot_im, NEB_MAXNREP, REAL, MPI_SUM, MPI_COMM_WORLD);
  Emax=-999999999999999;
  Emin=999999999999999;
  for(i=0;i<neb_nrep;i++)
	{
	  if(neb_epot_im[i]>=Emax)
	    {
	      Emax=neb_epot_im[i];
	      maximage=i;
	    }
 	  if(neb_epot_im[i]<=Emin)
	    {
	      Emin=neb_epot_im[i];
	    }
	}
  if(steps == neb_cineb_start)
    {
      if(neb_climbing_image > 0)
	{
	  if(myrank==0)
	    {
	      if( neb_climbing_image == maximage)
		printf("Starting climbing image = %d (= max_Epot = %lf)\n",neb_climbing_image, Emax);
	      else
		printf("Starting climbing image = %d \n WARNING: %d != %d with max_Epot = %lf)\n", \
		       neb_climbing_image,neb_climbing_image,maximage, Emax);
	    }
	}
      else
	{
	  neb_climbing_image = maximage;
	  if(myrank==0)
	    {
	      printf("Starting climbing image, image set to %d (= max_Epot = %lf)\n",maximage, Emax);
	    }
	}

    }

  /* exchange positions with neighbor replicas */
  
   
      neb_sendrecv_pos();
   
  
      dl2=0.0;d2=0.0;dr2=0.0;
  /* compute tangent of current NEB path */
      for (i=0; i<DIM*natoms; i+=DIM) {
	vektor dl, dr;
	real x;
	/* distance to left and right replica */
	dl.x = pos  [i  ] - pos_l[i  ];
	dl.y = pos  [i+1] - pos_l[i+1];
	dl.z = pos  [i+2] - pos_l[i+2];

	dr.x = pos_r[i  ] - pos  [i  ];
	dr.y = pos_r[i+1] - pos  [i+1];
	dr.z = pos_r[i+2] - pos  [i+2];
	
	
	
/* 	if(dl.x >= box_x.x/2.0) */
/* 	  { dl.x =  pos  [i  ] - box_x.x - pos_l[i  ];} */
/* 	else if(dl.x <= -box_x.x/2.0) */
/* 	  { dl.x =  pos  [i  ] + box_x.x - pos_l[i  ];} */
/* 	if(dl.y >= box_y.y/2.0) */
/* 	  { dl.y =  pos  [i+1  ] - box_y.y - pos_l[i+1  ];} */
/* 	else if(dl.y <= -box_y.y/2.0) */
/* 	  { dl.y =  pos  [i+1  ] + box_y.y - pos_l[i+1  ];} */
/* 	if(dl.z >= box_z.z/2.0) */
/* 	  { dl.z =  pos  [i+2  ] - box_z.z - pos_l[i+2  ];} */
/* 	else if(dl.z <= -box_z.z/2.0) */
/* 	  { dl.z =  pos  [i+2  ] + box_z.z - pos_l[i+2  ];} */
 
/* 	if(dr.x >= box_x.x/2.0) */
/* 	  { dr.x =  pos_r  [i  ] - box_x.x - pos[i  ];} */
/* 	else if(dr.x <= -box_x.x/2.0) */
/* 	  { dr.x =  pos_r  [i  ] + box_x.x - pos[i  ];} */
/* 	if(dr.y >= box_y.y/2.0) */
/* 	  { dr.y =  pos_r  [i+1  ] - box_y.y - pos[i+1  ];} */
/* 	else if(dr.y <= -box_y.y/2.0) */
/* 	  { dr.y =  pos_r  [i+1  ] + box_y.y - pos[i+1  ];} */
/* 	if(dr.z >= box_z.z/2.0) */
/* 	  { dr.z =  pos_r  [i+2  ] - box_z.z - pos[i+2  ];} */
/* 	else if(dr.z <= -box_z.z/2.0) */
/* 	  { dr.z =  pos_r  [i+2  ] + box_z.z - pos[i+2  ];} */
 

	/* apply periodic boundary conditions */
	if (1==pbc_dirs.x) {
	  x = - round( SPROD(dl,tbox_x) );
	  dl.x += x * box_x.x;
	  dl.y += x * box_x.y;
	  dl.z += x * box_x.z;
	  x = - round( SPROD(dr,tbox_x) );
	  dr.x += x * box_x.x;
	  dr.y += x * box_x.y;
	  dr.z += x * box_x.z;
	}
	if (1==pbc_dirs.y) {
	  x = - round( SPROD(dl,tbox_y) );
	  dl.x += x * box_y.x;
	  dl.y += x * box_y.y;
	  dl.z += x * box_y.z;
	  x = - round( SPROD(dr,tbox_y) );
	  dr.x += x * box_y.x;
	  dr.y += x * box_y.y;
	  dr.z += x * box_y.z;
	}
	if (1==pbc_dirs.z) {
	  x = - round( SPROD(dl,tbox_z) );
	  dl.x += x * box_z.x;
	  dl.y += x * box_z.y;
	  dl.z += x * box_z.z;
	  x = - round( SPROD(dr,tbox_z) );
	  dr.x += x * box_z.x;
	  dr.y += x * box_z.y;
	  dr.z += x * box_z.z;
    }
	//	#define OLDTANGENT
#ifdef OLDTANGENT
	/* unnormalized tangent vector */
	d[i  ] = dr.x + dl.x;
	d[i+1] = dr.y + dl.y;
	d[i+2] = dr.z + dl.z;
	

	/* unmodified spring force */
	f[i  ] = dr.x - dl.x; 
	f[i+1] = dr.y - dl.y; 
	f[i+2] = dr.z - dl.z; 
	
#else  /* now use improved tanget estimate (jcp 113 p9978) */


	/* unmodified spring force */
	f[i  ] = dr.x - dl.x; 
	f[i+1] = dr.y - dl.y; 
	f[i+2] = dr.z - dl.z; 
	

	V_previous = neb_epot_im[myimage-1];
	V_actual   = neb_epot_im[myimage];
	V_next     = neb_epot_im[myimage+1];
	
	
	// added by erik, i'm not totally sure of that
	/*   if(myimage==1) */
/* 	       { */
/* 		d[i  ] = dr.x ; */
/* 		d[i+1] = dr.y ; */
/* 		d[i+2] = dr.z ; */
/* 	      } */
/* 	    else if(myimage==neb_nrep-2) */
/* 	      { */
/* 		d[i  ] = dl.x ; */
/* 		d[i+1] = dl.y ; */
/* 		d[i+2] = dl.z ; */
/* 	      } */
/* 	    else */
	{
	  if ( ( V_next > V_actual ) && ( V_actual > V_previous ) )
	    {
	      d[i  ] = dr.x ;
	      d[i+1] = dr.y ;
	      d[i+2] = dr.z ;
	    }
	  else if ( ( V_next < V_actual ) && ( V_actual < V_previous ) ) 
	    {
	      d[i  ] = dl.x ;
	      d[i+1] = dl.y ;
	      d[i+2] = dl.z ;
	    }
	  else
	    {
	     
	      abs_next     = FABS( V_next     - V_actual );
	      abs_previous = FABS( V_previous - V_actual );
	      deltaVmax    = MAX( abs_next, abs_previous );
	      deltaVmin    = MIN( abs_next, abs_previous );
	      if (V_next > V_previous ) 
		{
		  d[i  ] = dr.x * deltaVmax + dl.x * deltaVmin ;
		  d[i+1] = dr.y * deltaVmax + dl.y * deltaVmin;
		  d[i+2] = dr.z * deltaVmax + dl.z * deltaVmin;
		}
	      else if ( V_next < V_previous ) 
		{
		  d[i  ] = dr.x * deltaVmin + dl.x * deltaVmax ;
		  d[i+1] = dr.y * deltaVmin + dl.y * deltaVmax;
		  d[i+2] = dr.z * deltaVmin + dl.z * deltaVmax;
		}
	      else
		{
		  d[i  ] = dr.x + dl.x;
		  d[i+1] = dr.y + dl.y;
		  d[i+2] = dr.z + dl.z;
		}
	    /*   if (myimage==5 && i==3) */
/* 		{ */
/* 		  printf("step %d rank %d Vprev %lf V act %lf Vnext %lf \n",steps,myrank, V_previous,V_actual,V_next); */
/* 		  printf("step %d rank %d absnext %lf absprev %lf dVmax %lf dVmin %lf \n",steps,myrank, abs_next,abs_previous,deltaVmax,deltaVmin); */
/* 		  printf("%le %le %le    ---    %le %le %le \n",d[i],d[i+1],d[i+2],dr.x + dl.x, dr.y + dl.y, dr.z + dl.z); */
/* 		  fflush(stdout); */
/* 		} */

	  /*     if (myimage == neb_climbing_image) */
/* 		{ */
/* 		  d[i  ] = dr.x + dl.x; */
/* 		  d[i+1] = dr.y + dl.y; */
/* 		  d[i+2] = dr.z + dl.z; */
/* 		} */

	    }
	}
#endif
     
      /* add up norms */
	dl2 += SPROD(dl,dl); 
	dr2 += SPROD(dr,dr); 
	drl += SPROD(dr,dl); 
	d2  += nebSPRODN(d+i,d+i); 
	drlmax = MAX(drlmax, fabs( SPROD(dr,dr) - SPROD(dl,dl) ) ); 
      } // end loop over i
  
  /* variable springs (jcp113 p. 9901) */
      if ( neb_kmax > 0 & neb_kmin >0 &&  steps > neb_vark_start)
	{
	  k_sum  = neb_kmax + neb_kmin;
	  k_diff = neb_kmax - neb_kmin;
	  tmp_neb_ks[myimage]=neb_kmin;
	  delta_E = Emax - Emin;
	  if (delta_E > 1.0e-32)
	    {
	      tmp_neb_ks[0] = 0.5*0.5 *(k_sum - k_diff * cos(3.141592653589793238*( neb_epot_im[0] - Emin )/delta_E ));
	      tmp_neb_ks[neb_nrep-1] =0.5* 0.5 *(k_sum - k_diff * cos(3.141592653589793238*( neb_epot_im[neb_nrep-1] - Emin )/delta_E ));
	      tmp_neb_ks[myimage] = 0.5 *(k_sum - k_diff * cos(3.141592653589793238*( neb_epot_im[myimage] - Emin )/delta_E ));
	    }
	}
      else
	{
	  tmp_neb_ks[myimage] = neb_k;
	}
      
      MPI_Allreduce(tmp_neb_ks , neb_ks, NEB_MAXNREP, REAL, MPI_SUM, MPI_COMM_WORLD);
      neb_ks[myimage] *= 0.5;
  




      /* project internal force onto perpendicular direction */
      tmp = 0.0;
      for (k=0; k<NCELLS; k++) {
	cell *p = CELLPTR(k);
	for (i=0; i<p->n; i++) { 
	  int n = NUMMER(p,i);
	  tmp += d X(n) * KRAFT(p,i,X);
	  tmp += d Y(n) * KRAFT(p,i,Y);
	  tmp += d Z(n) * KRAFT(p,i,Z);
	}
      }
       tmp /= -d2; // should be the wrong sign, but seems to work???
       //   tmp /= d2;
      for (k=0; k<NCELLS; k++) {
	cell *p = CELLPTR(k);
	for (i=0; i<p->n; i++) { 
	  int n = NUMMER(p,i);
	  //	  f2 += nebSPRODN( &KRAFT(p,i,X), &KRAFT(p,i,X) );
	  //      f2max = MAX( f2max, nebSPRODN( &KRAFT(p,i,X), &KRAFT(p,i,X) ) );
	  KRAFT(p,i,X) -= tmp * d X(n);
	  KRAFT(p,i,Y) -= tmp * d Y(n);
	  KRAFT(p,i,Z) -= tmp * d Z(n);
	  
	  if(myimage == neb_climbing_image && (steps >= neb_cineb_start))
	    {
	      KRAFT(p,i,X) -= tmp * d X(n);
	      KRAFT(p,i,Y) -= tmp * d Y(n);
	      KRAFT(p,i,Z) -= tmp * d Z(n);
	    }
	  
	}
      }
      
      /* estimate spring constant */
      /*   src[0] = sqrt(dr2); */
      /*   src[1] = sqrt(f2); */
      /*   if (0==myrank) src[0] += sqrt(dl2); */
      /*   src[2] = sqrt(d2 * f2max) / MAX(drlmax, 1e-3); */
      /*   MPI_Allreduce( src, dest, 3, REAL, MPI_SUM, MPI_COMM_WORLD); */
      /*   neb_k = dest[1] / dest[0]; */
      /*   neb_k = MAX(10,neb_k); */
      /*
	if ((fabs(neb_k - tmp) / neb_k) > 0.1) neb_k = tmp;
	neb_k = MAX(20,neb_k);
      */
      /*
	if (0==myrank) 
	printf("%d %e %e %e %e\n", nfc, sqrt(f2), fabs(dr2 - dl2), neb_k, 
	dest[2]/9 ); 
	neb_k = dest[2]/9;  
      */
      /*
	if (0==myrank) printf("%e %e\n", sqrt(d2 * f2), fabs(dr2 - dl2) ); 
	src[0] = sqrt(d2 * f2) / MAX( fabs(dr2 - dl2), 1e-6 );
	MPI_Allreduce( src, dest, 1, REAL, MPI_SUM, MPI_COMM_WORLD);
	neb_k = dest[0] / (neb_nrep - 2);
      */
      
      
      /* project spring force onto parallel direction */

#ifdef OLDTANGENT
      if(! ((steps >= neb_cineb_start) && (myimage == neb_climbing_image)))
	{
	  cosphi = drl / sqrt(dl2 * dr2);
	  if (cosphi > 0.0) fphi = 0.5 * (1.0 + cos(M_PI * cosphi));
	  else fphi = 1.0;
	  tmp   = (1 - fphi) * (dr2 - dl2) * neb_k / d2;
	  /* if (neb_nrep / 2 == myrank + 1) tmp = -tmp; */
	  
	  //  fphi *= neb_k;
	  fphi *= tmp_neb_ks[myimage];
	  for (k=0; k<NCELLS; k++) {
	    cell *p = CELLPTR(k);
	    for (i=0; i<p->n; i++) { 
	      int n = NUMMER(p,i);
	      KRAFT(p,i,X) += tmp * d X(n) + fphi * f X(n);
	      KRAFT(p,i,Y) += tmp * d Y(n) + fphi * f Y(n);
	      KRAFT(p,i,Z) += tmp * d Z(n) + fphi * f Z(n);
	    }
	  }
	}
#else
      if(! ((steps >= neb_cineb_start) && (myimage == neb_climbing_image)))
	{
	  normdl = SQRT(dl2);
	  normdr = SQRT(dr2);
	   inormd = -1.0/sqrt(d2);// should be the wrong sign, but seems to work???
	   // inormd = 1.0/sqrt(d2);// should be the wrong sign, but seems to work???

	  for (i=0; i<DIM*natoms; i+=DIM) {
	    f[i  ] = inormd *d[i]   * 0.5*( (neb_ks[myimage]+ neb_ks[myimage-1])*normdl - \
					    (neb_ks[myimage]+ neb_ks[myimage+1])*normdr );
	    f[i+1] = inormd *d[i+1] * 0.5*( (neb_ks[myimage]+ neb_ks[myimage-1])*normdl - \
					    (neb_ks[myimage]+ neb_ks[myimage+1])*normdr );
	    f[i+2] = inormd *d[i+2] * 0.5*( (neb_ks[myimage]+ neb_ks[myimage-1])*normdl - \
					    (neb_ks[myimage]+ neb_ks[myimage+1])*normdr );
	  }

	  for (k=0; k<NCELLS; k++)
	    {
	      cell *p = CELLPTR(k);
	      for (i=0; i<p->n; i++) 
		{ 
		  int n = NUMMER(p,i);
		
		  
		  KRAFT(p,i,X) += f X(n);
		  KRAFT(p,i,Y) += f Y(n);
		  KRAFT(p,i,Z) += f Z(n);
		}
	    }
	}
      
#endif
}

/******************************************************************************
*
*  write file with total fnorm, for monitoring convergence
*
******************************************************************************/
void write_neb_eng_file(int steps)
{
  static int flush_count=0;
  str255 fname;
  int i;

  /* write header */
  if (steps==0) {
    sprintf(fname, "%s.eng", neb_outfilename);
    neb_eng_file = fopen(fname,"a");
    if (NULL == neb_eng_file) 
      error_str("Cannot open properties file %s", fname);
    fprintf(neb_eng_file, "# nfc fnorm neb_k Epot_0 Epot_1 ... Epot_nrep\n");
  }

  /* open .eng file if not yet open */
  if (NULL == neb_eng_file) {
    sprintf(fname, "%s.eng", neb_outfilename);
    neb_eng_file = fopen(fname,"a");
    if (NULL == neb_eng_file) 
      error_str("Cannot open properties file %s.eng", outfilename);
  }

  fprintf(neb_eng_file, "%d %e %e   ", nfc, neb_fnorm, neb_k);
  for(i=0;i<neb_nrep;i++)
	{
	  fprintf(neb_eng_file,"%lf ", neb_epot_im[i]);
	}
  fprintf(neb_eng_file,"\n ");

  /* flush .eng file every flush_int writes */
  if (flush_count++ > flush_int) {
    fflush(neb_eng_file);
    flush_count=0;
  }
}

