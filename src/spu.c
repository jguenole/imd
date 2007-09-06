/* ISO C std. headers */
/* #include <stddef.h> */
/* #include <stdio.h> */



/* SPU headers */
#include <spu_mfcio.h>

/* Local/program specific headers */
#include "imd_cbe.h"



/* Is often needed */
typedef vector float  vecflt;


/* SPU (local) copy of pt which is initialized (once) via DMA and
   is needed by calc_wp
*/
pt_t pt;




/* Type of effective address used */
/* typedef unsigned long long  Tui64; 
typedef ea_t                Tea;
*/

/* Number of allocation units of size ausze needed for a block of size sze */
static INLINE_ unsigned nalloc_block(unsigned const sze, unsigned const ausze)
{   
    /* Number of units */
    unsigned const nau = sze/ausze;

    /* Have to add one more alloction unit? */
    return (((nau*ausze) == sze) ?  nau : (1u+nau));
}


/* Number of allocation units of size "ausze" needed for 
   nelem items of size itmsze
*/
static INLINE_ unsigned nalloc_items(unsigned const nelem, unsigned const itmsze,
                             unsigned const ausze)
{
     return nalloc_block(itmsze*nelem, ausze);
}














/* Allocate n items of size itmsze copying EAs
   The number of bytes actually allocated (which is a multiple of sizeof(Tau))
   is written to *isze.
*/
static void* (alloc)(unsigned const n,  unsigned const itmsze,
                     void** a, unsigned* const nrem,  unsigned const ausze,
                     unsigned const* ieasrc,  unsigned** ieadst,  unsigned** isze)
{
     /* Number of allocation units/bytes needed */
     unsigned const nalloc = nalloc_items(n,itmsze, ausze);
     unsigned const nbytes = nalloc*ausze;

     /* Enough memory? */
     if ( EXPECT_TRUE((*nrem)>=nalloc) ) {
         /* Allocation starts here */
         void* const allocstart = *a;

         /* Copy EA, updating the iterators */
          *(*ieadst) = *( ieasrc);
         ++(*ieadst);
         ++( ieasrc);
          *(*ieadst) = *( ieasrc);
         ++(*ieadst);

         /* Copy size (in bytes), updating iterator */
         *(*isze) = nbytes;
         ++(*isze);

         /* Update allocation pointer/number of units remaining */
         *a     = ((unsigned char*)(*a)) + nbytes;
         *nrem -= nalloc;

         /* Return ptr. to beginning of the block just allocated */
         return allocstart;
     }

     /* Error */
     return 0;
}






/* Given an exch_t build the corresponding wp_t, that is:
   Copy the scalar members
   Allocate memory for all the array members (using memory pointed to by a).
   Fetch all the input array data to LS using DMA (possibly a list DMA) with
   the specified tag
*/
static void start_create_wp(void** a, unsigned* nrem, unsigned const ausze,
                            exch_t const* const exch, wp_t* const wp,  unsigned const tag,
                            unsigned* pres_ea, unsigned* pres_sze)
{
     /* We need 4 EAs/sizes for the DMA + one high part of the EAs
        as well as some iterators over the EA/size arrays
     */
     enum {  Nea=4u };
     unsigned ALIGNED_(8, ea[(Nea<<1u)+1u]), sze[Nea];
     unsigned *pea=ea, *psze=sze;





     /* Macros to allocate and to DMA members */
#    define ALLOCMEMB(mb,ty,nele)                                         \
   if ( EXPECT_FALSE(0 == ((wp->mb)=(ty*)alloc((nele), (sizeof (ty)), a,nrem, ausze, (exch->mb), &pea, &psze))) ) {   \
        return;                       \
   }

     /* #    define DMAMEMB(member,nn) DMA64(wp->member,sze[nn], exch->member, tag, MFC_GET_CMD)
      */
#define DMAMEMB(member,nn)   spu_mfcdma64(wp->member, (exch->member)[0], (exch->member)[1],  sze[nn],  tag,  MFC_GET_CMD)

     ALLOCMEMB(pos, vecflt,  exch->n2)
     DMAMEMB(pos, 0);
     ALLOCMEMB(typ, int,     exch->n2)
     DMAMEMB(typ, 1);
     ALLOCMEMB(ti,  int,   2*exch->n2)
     DMAMEMB(ti,  2);
     ALLOCMEMB(tb,  short,   exch->len)
     DMAMEMB(tb,  3);

#undef DMAMEMB
#undef ALLOCMEMB


     


     /* Also allocate LS memory for force, copying its ea and alloction size */
     wp->force = (vecflt*)(alloc(exch->n2, (sizeof (vecflt)),
                                 a,nrem,ausze, exch->force, &pres_ea, &pres_sze
                                )
                          );


     /* Now copy the scalar members from exch to wp */
#define CPYMEMB(member) ((wp->member)=(exch->member))
     CPYMEMB(k);
     CPYMEMB(n1);
     CPYMEMB(n1_max);
     CPYMEMB(n2);
     CPYMEMB(n2_max);
     CPYMEMB(len);
     CPYMEMB(len_max);

     /* No need to copy totpot & virial as it is initialized in calc_wp
        CPYMEMB(totpot);
        CPYMEMB(virial);
     */
#undef CPYMEMB
}





static void start_create_pt(void** a, unsigned* nrem, unsigned const ausze,
                            env_t const* const env, pt_t* const p,
                            unsigned const tag)
{
     /* There are 4 pointers/EAs in pt_t/env_t
        Every pointer in env points to nelem items of type flt, so
        there are 4*nelem itmes to be DMAed in total
     */
     unsigned const nelem   = (env->ntypes)*(env->ntypes);
     /* Total number of bytes/allocation units needed */
     unsigned const nszetot = 4*nelem*(sizeof (vecflt));
     unsigned const nau = nalloc_block(nszetot, ausze);

     /* Enough memory available? */
     if ( EXPECT_TRUE((*nrem)>=nau) ) {
         /* Start DMAing the memory pointed to by pt.r2cut to LS, also getting data
            for lj_sig,... which follows after that in main mem. */
 
        /* DMA64((*a), nszetot, env->r2cut,  tag, MFC_GET_CMD); */
        spu_mfcdma64(*a, (env->r2cut)[0], (env->r2cut)[1], nszetot, tag, MFC_GET_CMD);

        /* Set the pointers in the resulting pt_t (see modified allocation scheme
           in mk_pt)
	*/
        p->r2cut    = ((vecflt *)(*a));
        p->lj_sig   = p->r2cut  + nelem;
        p->lj_eps   = p->lj_sig + nelem;
        p->lj_shift = p->lj_eps + nelem;

	/* Copy the scalar member */
        p->ntypes   = env->ntypes;

        /* Update alloction pointer/remaing size */
        (*a)   = ((unsigned char*)(*a)) + (nau*ausze);
        *nrem -= nau;
     }
}


static void start_init(void** a, unsigned* nrem, unsigned const ausze,
                       unsigned const envea[],
                       pt_t* const ppt, unsigned const tag)
{
    /* First DMA env which is only neede here */
    env_t ALIGNED_(16, env);
    spu_mfcdma64(&env,  envea[0],envea[1], (sizeof env),  5u, MFC_GET_CMD);
    spu_writech(MFC_WrTagMask, (1u<<5u));
    spu_mfcstat(MFC_TAG_UPDATE_ALL);

    /* Start DMA of pt */
    start_create_pt(a,nrem, ausze,  &env, ppt,  tag);
}



/* DMA forces back to main memory */
static void start_DMA_results(unsigned const* const exch_ea, unsigned const tag,
                              wp_t const* const wp, exch_t* const exch,
                              unsigned const* const res_sze
                             )
{
     /* First DMA the force array directly back to main memory... */
     /* DMA64(wp->force, res_sze[0],  exch->force,  tag, MFC_PUT_CMD); */
     spu_mfcdma64(wp->force, (exch->force)[0], (exch->force)[1],  res_sze[0],  tag, MFC_PUT_CMD);

     /* In the meantime:
        Copy the scalars which have been updated in wp back to exch
        Leaving all the other scalar members unchanged
     */
     exch->totpot = wp->totpot;
     exch->virial = wp->virial;

     /* ...then DMA the updated exch_t */
     /* DMA64(exch, (sizeof (*exch)), exch_ea, tag, MFC_PUT_CMD); */
     spu_mfcdma64(exch, exch_ea[0], exch_ea[1],  (sizeof (*exch)),  tag, MFC_PUT_CMD);
}



/* Argument types for main function */
typedef unsigned long long ui64_t;

typedef union {
    ui64_t   ea64;
    unsigned ea32[(sizeof (ui64_t)) / (sizeof (unsigned))];
} Targ, Tenv;





int main(ui64_t const id, Targ const argp, Tenv const envp)
{
    /* Unsigned integer constant, e.g. needed for tags/masks */
    typedef unsigned const Tuc;

    /* Some tags and the correspondning masks */
    static Tuc intag[]  = {      1u,       2u  };
    static Tuc inmsk[]  = { (1u<<1u), (1u<<2u) };
    static Tuc outtag[] = {      3u,       4u  };
    static Tuc outmsk[] = { (1u<<3u), (1u<<4u) };

 
    /* Local memory for DMAs (allocated "dynamically" on the stack) */
    enum { allocsize=16u };
    unsigned char ALIGNED_(16, DMAmem[90u*1024u]);
 
    /* Number of alloction units remaining, current point of allocation */
    unsigned DMArem=sizeof(DMAmem), DMArem0;
    void    *DMAloc=DMAmem,        *DMAloc0;



    /* Set decrementer to max. value */
    spu_writech(SPU_WrDec, (~(0u)));


    /* Fetch env and start creating the corresponding pt_t (Using DMA) */
    start_init(&DMAloc, &DMArem, allocsize, envp.ea32,  &pt, intag[0]);

    /* Store allocation status after pt has been allocated on the stack */
    DMAloc0 = DMAloc;
    DMArem0 = DMArem;


    /* fprintf(stdout, "Starting work loop on SPU\n"); fflush(stdout); */

    /* Work loop */
    for(;;) {
        /* Local copies of work packages on the SPU & the exch control block */
        wp_t wp[1];
        exch_t ALIGNED_(16, exch[1]);

        /* EAs & sizes of the results, which at the moment is 
          the force vector only */
        enum { Nres=1u };
        unsigned res_ea[Nres<<1u], res_sze[Nres];



        /* Wait for signal from PPU */
        if ( EXPECT_FALSE(SPUEXIT==spu_read_in_mbox()) ) {
  	    return 0;
        }

        /* Fetch exch controll block: Start DMA & wait for it to complete */
        /* DMA64(&(exch[0]), (sizeof exch[0]), argp.ea32, 5u, MFC_GET_CMD); */
        spu_mfcdma64(&(exch[0]),  (argp.ea32)[0],(argp.ea32)[1],   (sizeof (exch[0])), 5u,  MFC_GET_CMD);
        spu_writech(MFC_WrTagMask,  (1u<<5u));
        spu_mfcstat(MFC_TAG_UPDATE_ALL);


        /* fprintf(stdout, "Got control block\n"); fflush(stdout); */



        /* Start the creation (via DMA) of a wp_t using the exch_t  & 
           wait for it to finish 
	*/
        DMAloc=DMAloc0;
        DMArem=DMArem0;

        start_create_wp(&DMAloc,&DMArem, allocsize, &(exch[0]),&(wp[0]),  intag[0],
                        res_ea,res_sze);
        spu_writech(MFC_WrTagMask,  inmsk[0]);
        spu_mfcstat(MFC_TAG_UPDATE_ALL);


        /* The main work to be done */
        calc_wp(&(wp[0]));


        /* DMA results back to main memory & wait for it to complete */
        start_DMA_results(argp.ea32, outtag[0], &(wp[0]), &(exch[0]), res_sze);
        spu_writech(MFC_WrTagMask, outmsk[0]);
        spu_mfcstat(MFC_TAG_UPDATE_ALL);


        /* Done calculating & transferring wp */
        spu_write_out_mbox(WPDONE1);
    }

    /* We should not arrive here as we only leave the main program if
       SPUEXIT is passed via mbox */
    return 1;
}