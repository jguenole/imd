
/******************************************************************************
*
*  IMD -- The ITAP Molecular Dynamics Program
*
*  Copyright 1996-2001 Institute for Theoretical and Applied Physics,
*  University of Stuttgart, D-70550 Stuttgart
*
*  $Revision$
*  $Date$
*
******************************************************************************/

/******************************************************************************
*
*  atdist is a utility program to analyse/convert atoms distribution files.
*
*  Compilation: 
*
*    gcc -O -o atdist atdist.c -lm
*
*  Usage for 2D distributions:
*
*    atdist <file>
*
*  writes a pgm file of the distribution for each atom type, and a ppm file
*  if there are not more than tree atom types. The atoms types are then
*  encoded in red, green, and blue.
*
*  Usage for 3D distributions:
*
*    atdist <file>
*
*  projects the distribution onto the three axis, and writes these 1D
*  histograms to the terminal. This helps to find out where the dense
*  planes in the distribution are.
*
*    atdist <file> <dir> <min> <max>
*
*  adds the slices from <min> to <max> perpendicular to <dir>, and writes 
*  a pgm file for each atom type, and a ppm file if there are not more than 
*  tree atom types. The atoms types are then encoded in red, green, and blue.
*
*    atdist <file> <xmin> <ymin> <zmin> <xmax> <ymax> <zmax>
*
*  cuts a rectangular block from the volume and writes it in virvo xvf format.
*
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

float *atoms_dist, ddx, ddy, ddz;;
int dim, dimx, dimy, dimz, ntypes, size, my_endian;

/******************************************************************************
*
*  return error message and stop
*
******************************************************************************/

void error(char *msg)
{
  fprintf(stderr,"Error: %s\n",msg);
  exit(2);
}

/******************************************************************************
*
*  endian returns 1 if system is big endian, 0 if little endian
*
******************************************************************************/

int endian(void)
{
  unsigned short int word = 0x0001;
  unsigned char  *byte    = (unsigned char *) &word;
  return (byte[0] ? 0 : 1);
}

/******************************************************************************
*
*  do endian swap for a four byte quantity
*
******************************************************************************/

void do_endian_swap_4(char *str)
{
  char *c;
  *c = * str;    * str    = *(str+3); *(str+3) = *c;
  *c = *(str+1); *(str+1) = *(str+2); *(str+2) = *c;
}

/******************************************************************************
*
*  copy 2 bytes to big endian
*
******************************************************************************/

void copy2bytes(char *str, void *source)
{
  char *c;
  c = source;
  if (my_endian==1) {
    * str    = * c   ;
    *(str+1) = *(c+1);
  } else {
    * str    = *(c+1);
    *(str+1) = * c   ;
  }
}

/******************************************************************************
*
*  copy 4 bytes to big endian
*
******************************************************************************/

void copy4bytes(char *str, void *source)
{
  char *c;
  c = source;
  if (my_endian==1) {
    * str    = * c   ;
    *(str+1) = *(c+1);
    *(str+2) = *(c+2);
    *(str+3) = *(c+3);
  } else {
    * str    = *(c+3);
    *(str+1) = *(c+2);
    *(str+2) = *(c+1);
    *(str+3) = * c   ;
  }
}

/******************************************************************************
*
*  read atoms distribution
*
******************************************************************************/

int read_atoms_dist(char *fname)
{
  FILE *infile;
  char line[255];
  int  cont, input_endian, i;

  /* open file */
  if (NULL==(infile=fopen(fname,"r"))) error("Cannot open input file");

  /* read file header */
  cont=1;
  do {
    fgets(line,255,infile);
    if (line[0]!='#') {
      error("file header corrupt!");
    } else {
      if (line[1]=='F') {
        sscanf(line+2,"%d %d %d",&input_endian,&dim,&ntypes);
      } else  if (line[1]=='D') {
        if (dim!=sscanf(line+2,"%d %d %d",&dimx,&dimy,&dimz))
          error("file header corrupt (dimension)!");
      } else  if (line[1]=='S') {
        if (dim!=sscanf(line+2,"%f %f %f",&ddx,&ddy,&ddz))
          error("file header corrupt (dimension)!");
      } else  if (line[1]=='E') {
	cont=0;
      }
    }
  } while (cont);
  
  /* compute array size */
  size = dimx * dimy;
  if (dim==3) size *= dimz;
  size *= ntypes;

  /* allocate atoms distribution array */
  atoms_dist = (float *) malloc( size * sizeof(float) );
  if (NULL==atoms_dist) error("Cannot allocate atoms distribution array");

  /* read atoms distribution data */
  if (size!=fread(atoms_dist,sizeof(float),size,infile))
    error("Cannot read histogram data");

  /* close input file */
  fclose(infile);

  /* do endian swap if necessary */
  if (input_endian!=endian()) {
    for (i=0; i<size; i++) do_endian_swap_4((char *)(atoms_dist+i));
  }

  return dim;
}

/******************************************************************************
*
*  project histogram on the three main axis
*
******************************************************************************/

void axis_projections_3d()
{
  float *histx, *histy, *histz;
  int   t, i, j, k, l;

  histx = (float *) calloc( ntypes * dimx, sizeof(float) );
  histy = (float *) calloc( ntypes * dimy, sizeof(float) );
  histz = (float *) calloc( ntypes * dimz, sizeof(float) );
  if ((histx==NULL) || (histy==NULL) || (histx==NULL))
    error("out of memory");

  for (t=0; t<ntypes; t++)
    for (i=0; i<dimx; i++)
      for (j=0; j<dimy; j++)
        for (k=0; k<dimz; k++) {
          l = ((t * dimx + i) * dimy + j) * dimz + k;
          histx[t * dimx + i] += atoms_dist[l];
          histy[t * dimy + j] += atoms_dist[l];
          histz[t * dimz + k] += atoms_dist[l];
	}

  printf("# Projection on x-Axis:\n");
  for (i=0; i<dimx; i++) {
    printf("%d",i);
    for (t=0; t<ntypes; t++) printf(" %e", histx[t * dimx + i]);
    printf("\n");
  }

  printf("\n\n# Projection on y-Axis:\n");
  for (i=0; i<dimy; i++) {
    printf("%d",i);
    for (t=0; t<ntypes; t++) printf(" %e", histy[t * dimy + i]);
    printf("\n");
  }

  printf("\n\n# Projection on z-Axis:\n");
  for (i=0; i<dimz; i++) {
    printf("%d",i);
    for (t=0; t<ntypes; t++) printf(" %e", histz[t * dimz + i]);
    printf("\n");
  }
}

/******************************************************************************
*
*  write pictures of xy-slices
*
******************************************************************************/

void xy_pictures_3d(char *infile, int min, int max)
{
  float *hist, fmax;
  char  *pix, *pix2;
  int   dimxy, t, i, j, k, l;
  FILE  *out;
  char  fname[255]; 

  dimxy  = dimx  * dimy;
  hist = (float *) calloc( ntypes * dimxy, sizeof(float) );
  pix  = (char  *) calloc( ntypes * dimxy, sizeof(char ) );
  pix2 = (char  *) calloc( 3      * dimxy, sizeof(char ) );
  if ((hist==NULL) || (pix==NULL) || (pix2==NULL)) error("out of memory");


  for (t=0; t<ntypes; t++)
    for (i=0; i<dimx; i++)
      for (j=0; j<dimy; j++)
        for (k=min; k<max+1; k++) {
          l = ((t * dimx + i) * dimy + j) * dimz + k;
          hist[(t * dimy + j) * dimx + i] += atoms_dist[l];
	}

  /* renormalize atoms distribution for pgm files */
  fmax = 0.0;
  for (i=0; i<ntypes*dimxy; i++) fmax = MAX(fmax,hist[i]);
  fmax = 255/fmax;
  for (i=0; i<ntypes*dimxy; i++) pix[i] = (char) (255-hist[i]*fmax);

  /* write pgm files */
  for (t=0; t<ntypes; t++) {
    sprintf(fname,"%s.xy.%d.pgm",infile,t);
    if (NULL==(out=fopen(fname,"w"))) 
      error("Cannot open pgm-file.");
    fprintf(out,"P5\n%d %d\n255\n", dimx, dimy);
    if (dimxy!=fwrite(pix+t*dimxy,sizeof(char),dimxy,out))
      error("Cannot write pgm-file.");
    fclose(out);
  }

  /* write ppm-file */
  if (ntypes<4) {
    /* set everything to white */
    for (i=0; i<3*dimxy; i++) pix2[i] = 255;
    /* get renormalization factor for color pictures */
    fmax = 0.0;
    for (i=0; i<dimxy; i++) {
      float tmp=0.0;
      for (t=0; t<ntypes; t++) tmp += hist[t*dimxy+i];
      fmax = MAX(fmax,tmp);
    }
    fmax = 255/fmax;
    /* make color picture */
    for (t=0; t<ntypes; t++) 
      for (i=0; i<dimxy; i++) 
        for (k=0; k<3; k++) 
          if (k!=t) pix2[3*i+k] -= (char) (hist[t*dimxy+i]*fmax);
    /* write color picture */
    sprintf(fname,"%s.xy.ppm",infile);
    if (NULL==(out=fopen(fname,"w"))) error("Cannot open ppm-file.");
    fprintf(out,"P6\n%d %d\n255\n", dimx, dimy);
    if (3*dimxy!=fwrite(pix2,sizeof(char),3*dimxy,out))
      error("Cannot write ppm-file.");
    fclose(out);
  }
}

/******************************************************************************
*
*  write pictures of xz-slices
*
******************************************************************************/

void xz_pictures_3d(char *infile, int min, int max)
{
  float *hist, fmax;
  char  *pix, *pix2;
  int   dimxz, t, i, j, k, l;
  FILE  *out;
  char  fname[255]; 

  dimxz  = dimx  * dimz;
  hist = (float *) calloc( ntypes * dimxz, sizeof(float) );
  pix  = (char  *) calloc( ntypes * dimxz, sizeof(char ) );
  pix2 = (char  *) calloc( 3      * dimxz, sizeof(char ) );
  if ((hist==NULL) || (pix==NULL) || (pix2==NULL)) error("out of memory");

  for (t=0; t<ntypes; t++)
    for (i=0; i<dimx; i++)
      for (j=min; j<max+1; j++)
        for (k=0; k<dimz; k++) {
          l = ((t * dimx + i) * dimy + j) * dimz + k;
          hist[(t * dimz + k) * dimx + i] += atoms_dist[l];
	}

  /* renormalize atoms distribution for pgm files */
  fmax = 0.0;
  for (i=0; i<ntypes*dimxz; i++) fmax = MAX(fmax,hist[i]);
  fmax = 255/fmax;
  for (i=0; i<ntypes*dimxz; i++) pix[i] = (char) (255-hist[i]*fmax);

  /* write pgm files */
  for (t=0; t<ntypes; t++) {
    sprintf(fname,"%s.xz.%d.pgm",infile,t);
    if (NULL==(out=fopen(fname,"w"))) 
      error("Cannot open pgm-file.");
    fprintf(out,"P5\n%d %d\n255\n", dimx, dimz);
    if (dimxz!=fwrite(pix+t*dimxz,sizeof(char),dimxz,out))
      error("Cannot write pgm-file.");
    fclose(out);
  }

  /* write ppm-file */
  if (ntypes<4) {
    /* set everything to white */
    for (i=0; i<3*dimxz; i++) pix2[i] = 255;
    /* get renormalization factor for color pictures */
    fmax = 0.0;
    for (i=0; i<dimxz; i++) {
      float tmp=0.0;
      for (t=0; t<ntypes; t++) tmp += hist[t*dimxz+i];
      fmax = MAX(fmax,tmp);
    }
    fmax = 255/fmax;
    /* make color picture */
    for (t=0; t<ntypes; t++) 
      for (i=0; i<dimxz; i++) 
        for (k=0; k<3; k++) 
          if (k!=t) pix2[3*i+k] -= (char) (hist[t*dimxz+i]*fmax);
    /* write color picture */
    sprintf(fname,"%s.xz.ppm",infile,t);
    if (NULL==(out=fopen(fname,"w"))) error("Cannot open ppm-file.");
    fprintf(out,"P6\n%d %d\n255\n", dimx, dimz);
    if (3*dimxz!=fwrite(pix2,sizeof(char),3*dimxz,out))
      error("Cannot write ppm-file.");
    fclose(out);
  }
}

/******************************************************************************
*
*  write pictures of yz-slices
*
******************************************************************************/

void yz_pictures_3d(char *infile, int min, int max)
{
  float *hist, fmax;
  char  *pix, *pix2;
  int   dimyz, t, i, j, k, l;
  FILE  *out;
  char  fname[255]; 

  dimyz  = dimy  * dimz;
  hist = (float *) calloc( ntypes * dimyz, sizeof(float) );
  pix  = (char  *) calloc( ntypes * dimyz, sizeof(char ) );
  pix2 = (char  *) calloc( 3      * dimyz, sizeof(char ) );
  if ((hist==NULL) || (pix==NULL) || (pix2==NULL)) error("out of memory");

  for (t=0; t<ntypes; t++)
    for (i=min; i<max+1; i++)
      for (j=0; j<dimy; j++)
        for (k=0; k<dimz; k++) {
          l = ((t * dimx + i) * dimy + j) * dimz + k;
          hist[(t * dimz + k) * dimy + j] += atoms_dist[l];
	}

  /* renormalize atoms distribution for pgm files */
  fmax = 0.0;
  for (i=0; i<ntypes*dimyz; i++) fmax = MAX(fmax,hist[i]);
  fmax = 255/fmax;
  for (i=0; i<ntypes*dimyz; i++) pix[i] = (char) (255-hist[i]*fmax);

  /* write pgm files */
  for (t=0; t<ntypes; t++) {
    sprintf(fname,"%s.yz.%d.pgm",infile,t);
    if (NULL==(out=fopen(fname,"w"))) 
      error("Cannot open pgm-file.");
    fprintf(out,"P5\n%d %d\n255\n", dimy, dimz);
    if (dimyz!=fwrite(pix+t*dimyz,sizeof(char),dimyz,out))
      error("Cannot write pgm-file.");
    fclose(out);
  }

  /* write ppm-file */
  if (ntypes<4) {
    /* set everything to white */
    for (i=0; i<3*dimyz; i++) pix2[i] = 255;
    /* get renormalization factor for color pictures */
    fmax = 0.0;
    for (i=0; i<dimyz; i++) {
      float tmp=0.0;
      for (t=0; t<ntypes; t++) tmp += hist[t*dimyz+i];
      fmax = MAX(fmax,tmp);
    }
    fmax = 255/fmax;
    /* make color picture */
    for (t=0; t<ntypes; t++) 
      for (i=0; i<dimyz; i++) 
        for (k=0; k<3; k++) 
          if (k!=t) pix2[3*i+k] -= (char) (hist[t*dimyz+i]*fmax);
    /* write color picture */
    sprintf(fname,"%s.yz.ppm",infile);
    if (NULL==(out=fopen(fname,"w"))) error("Cannot open ppm-file.");
    fprintf(out,"P6\n%d %d\n255\n", dimy, dimz);
    if (3*dimyz!=fwrite(pix2,sizeof(char),3*dimyz,out))
      error("Cannot write ppm-file.");
    fclose(out);
  }
}

/******************************************************************************
*
*  write pictures of 2d histogram
*
******************************************************************************/

void pictures_2d(char *infile)
{
  float *hist, fmax;
  char  *pix, *pix2;
  int dimxy, t, i, j, k, l;
  FILE *out;
  char fname[255]; 

  dimxy = dimx * dimy;
  hist = (float *) calloc( ntypes * dimxy, sizeof(float) );
  pix  = (char  *) calloc( ntypes * dimxy, sizeof(char ) );
  pix2 = (char  *) calloc( 3      * dimxy, sizeof(char ) );
  if ((hist==NULL) || (pix==NULL) || (pix2==NULL)) error("out of memory");

  for (t=0; t<ntypes; t++)
    for (i=0; i<dimx; i++)
      for (j=0; j<dimy; j++) {
          l =  (t * dimx + i) * dimy + j;
          hist[(t * dimy + j) * dimx + i] = atoms_dist[l];
	}

  /* renormalize atoms distribution for pgm files */
  fmax = 0.0;
  for (i=0; i<ntypes*dimxy; i++) fmax = MAX(fmax,hist[i]);
  fmax = 255/fmax;
  for (i=0; i<ntypes*dimxy; i++) pix[i] = (char) (255-hist[i]*fmax);

  /* write pgm files */
  for (t=0; t<ntypes; t++) {
    sprintf(fname,"%s.%d.pgm",infile,t);
    if (NULL==(out=fopen(fname,"w"))) error("Cannot open pgm-file.");
    fprintf(out,"P5\n%d %d\n255\n", dimx, dimy);
    if (dimxy!=fwrite(pix+t*dimxy,sizeof(char),dimxy,out))
      error("Cannot write pgm-file.");
    fclose(out);
  }

  /* write ppm-file */
  if (ntypes<4) {
    /* set everything to white */
    for (i=0; i<3*dimxy; i++) pix2[i] = 255;
    /* get renormalization factor for color pictures */
    fmax = 0.0;
    for (i=0; i<dimxy; i++) {
      float tmp=0.0;
      for (t=0; t<ntypes; t++) tmp += hist[t*dimxy+i];
      fmax = MAX(fmax,tmp);
    }
    fmax = 255/fmax;
    /* make color picture */
    for (t=0; t<ntypes; t++) 
      for (i=0; i<dimxy; i++) 
        for (k=0; k<3; k++) 
          if (k!=t) pix2[3*i+k] -= (char) (hist[t*dimxy+i]*fmax);
    /* write color picture */
    sprintf(fname,"%s.ppm",infile);
    if (NULL==(out=fopen(fname,"w"))) error("Cannot open ppm-file.");
    fprintf(out,"P6\n%d %d\n255\n", dimx, dimy);
    if (3*dimxy!=fwrite(pix2,sizeof(char),3*dimxy,out))
      error("Cannot write ppm-file.");
    fclose(out);
  }
}

/******************************************************************************
*
*  virvo volume data
*
******************************************************************************/

void virvo_picture_3d(char *infile, int min_x, int min_y, int min_z,
                                    int max_x, int max_y, int max_z)
{
  float *hist, fmax, fl;
  unsigned char *vol;
  int   len, len4, dx, dy, dz, t, i, j, k, l, m;
  unsigned short us;
  unsigned int   ui;
  unsigned char  uc;
  char  fname[255], header[48], *str; 
  FILE  *out;

  min_x = MAX(min_x,0); max_x = MIN(max_x,dimx); dx = max_x - min_x;
  min_y = MAX(min_y,0); max_y = MIN(max_y,dimy); dy = max_y - min_y;
  min_z = MAX(min_z,0); max_z = MIN(max_z,dimz); dz = max_z - min_z;
  len   = dx * dy * dz;
  len4  = 4 * len;

  /* make volume file header */
  my_endian=endian();
  str = header;
  sprintf(str,"%s","VIRVO-XVF");      str +=9;   /* file type */
  us = 48; copy2bytes(str,&us);       str +=2;   /* header size */
  ui = dx; copy4bytes(str,&ui);       str +=4;   /* dim_x */
  ui = dy; copy4bytes(str,&ui);       str +=4;   /* dim_y */
  ui = dz; copy4bytes(str,&ui);       str +=4;   /* dim_z */
  ui =  1; copy4bytes(str,&ui);       str +=4;   /* number of frames */
  uc = 32; (unsigned char) *str = uc; str++;     /* bits per voxel */
  fl = 1.0/ddx; copy4bytes(str,&fl);  str +=4;   /* x-length of voxel */
  fl = 1.0/ddy; copy4bytes(str,&fl);  str +=4;   /* y-length of voxel */
  fl = 1.0/ddz; copy4bytes(str,&fl);  str +=4;   /* z-length of voxel */
  fl = 1.0;     copy4bytes(str,&fl);  str +=4;   /* secs per frame */
  us =  0; copy2bytes(str,&us);       str +=2;   /* number of transf. func. */
  us =  0; copy2bytes(str,&us);       str +=2;   /* type of transf. func. */

  hist = (float          *) calloc( len,  sizeof(float) );
  vol  = (unsigned char  *) calloc( len4, sizeof(char ) );
  if ((hist==NULL) || (vol==NULL)) error("out of memory");

  /* compute renormalization factor */
  for (t=0; t<ntypes; t++)
    for (i=min_x; i<max_x; i++)
      for (j=min_y; j<max_y; j++)
        for (k=min_z; k<max_z; k++) {
          l = ((t * dimx + i) * dimy + j) * dimz + k;
          m = ((k-min_z) * dy + (j-min_y)) * dx + (i-min_x);
          hist[m] += atoms_dist[l];
	}
  fmax = 0.0;
  for (i=0; i<len; i++) fmax = MAX(fmax,hist[i]);
  fmax = 255/fmax;

  /* compute volume data */
  for (t=0; t<ntypes; t++)
    for (i=min_x; i<max_x; i++)
      for (j=min_y; j<max_y; j++)
        for (k=min_z; k<max_z; k++) {
          l = ((t * dimx + i) * dimy + j) * dimz + k;
          m = ((k-min_z) * dy + (j-min_y)) * dx + (i-min_x);
          vol[4*m+t] = (unsigned char) (atoms_dist[l]*fmax);
          vol[4*m+3] = (unsigned char) (hist[m]      *fmax);
	}

  /* make all colors bright */
  for (i=0; i<len; i++) {
    if (vol[4*i]<vol[4*i+1]) {
      vol[4*i]=0;
      if (vol[4*i+1]<vol[4*i+2]) {
        vol[4*i+1]=  0;
        vol[4*i+2]=255;
      } else {
        vol[4*i+2]=  0;
        vol[4*i+1]=255;
      }
    } else {
      vol[4*i+1]=0;
      if (vol[4*i]<vol[4*i+2]) {
        vol[4*i  ]=  0;
        vol[4*i+2]=255;
      } else {
        vol[4*i+2]=  0;
        vol[4*i  ]=255;
      }
    }
  }

  /* write volume data */
  sprintf(fname,"%s.xvf",infile);
  if (NULL==(out=fopen(fname,"w"))) 
    error("Cannot open volume file.");
  if (48!=fwrite(header,sizeof(char),48,out))
    error("Cannot write volume file header.");
  if (len4!=fwrite(vol,sizeof(char),len4,out))
    error("Cannot write volume file data.");
  fclose(out);

}

/******************************************************************************
*
*  main
*
******************************************************************************/

int main(int argc, char **argv) 
{
  int dim, dir=0, min, max;
  int min_x, min_y, min_z, max_x, max_y, max_z;

  /* check number of arguments */
  if ((argc!=2) && (argc!=5) && (argc!=8)) {
     printf("Usage:  %s infile [dir min max]\n",argv[0]);
     printf("        %s infile xmin ymin zmin xmax ymax zmax\n",argv[0]);
     exit(1);
  }

  /* scan further arguments */
  if (argc==5) {
    sscanf(argv[2], "%d", &dir);
    sscanf(argv[3], "%d", &min); 
    sscanf(argv[4], "%d", &max);
  }
  if (argc==8) {
    sscanf(argv[2], "%d", &min_x);
    sscanf(argv[3], "%d", &min_y); 
    sscanf(argv[4], "%d", &min_z);
    sscanf(argv[5], "%d", &max_x);
    sscanf(argv[6], "%d", &max_y); 
    sscanf(argv[7], "%d", &max_z);
  }

  /* read atom distribution */
  dim = read_atoms_dist(argv[1]);
  if ((dim!=3) && (argc==8)) {
    error("This mode is for 3D data only!");
  }

  /* write summary or pictures */
  if ((argc==2) && (dim==3)) {
    axis_projections_3d();
  } else  if ((argc==2) && (dim==2)) {
      pictures_2d(argv[1]);
  } else {
    if (dir==3) {
      xy_pictures_3d(argv[1],min,max);
    } else if (dir==2) { 
      xz_pictures_3d(argv[1],min,max);
    } else if (dir==1) {
      yz_pictures_3d(argv[1],min,max);
    } else if (dir==0) {
      virvo_picture_3d(argv[1],min_x,min_y,min_z,max_x,max_y,max_z);
    }
  }
  return 0;
}
