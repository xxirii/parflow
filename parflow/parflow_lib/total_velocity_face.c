/*BHEADER**********************************************************************
 * (c) 1995   The Regents of the University of California
 *
 * See the file COPYRIGHT_and_DISCLAIMER for a complete copyright
 * notice, contact person, and disclaimer.
 *
 * $Revision: 1.1.1.1 $
 *********************************************************************EHEADER*/
/******************************************************************************
 *
 *****************************************************************************/

#include "parflow.h"

#define Func(a, b) HarmonicMean(a, b)
#define Coeff(Ta, Tb, Pa, Pb) \
( ((Ta) + (Tb)) ? ((Ta)*(Pb) + (Tb)*(Pa))/((Ta) + (Tb)) : 0 )

#define FuncOps     4
#define CoeffOps    5

/*--------------------------------------------------------------------------
 * Structures
 *--------------------------------------------------------------------------*/

typedef struct
{
   int          time_index;

} PublicXtra;

typedef struct
{
   PFModule          *phase_mobility;
   PFModule          *capillary_pressure;
   PFModule          *phase_density;

   Problem           *problem;
   Grid              *grid;
   Grid              *x_grid;
   Grid              *y_grid;
   Grid              *z_grid;
   double            *temp_data;

   Vector   *temp_pressure;
   Vector   *temp_mobility_x;
   Vector   *temp_mobility_y;
   Vector   *temp_mobility_z;

} InstanceXtra;


/*--------------------------------------------------------------------------*
 *                         TotalVelocityFace                                *
 *--------------------------------------------------------------------------*/

void    TotalVelocityFace(xvel, yvel, zvel, problem_data,
			  total_mobility_x, total_mobility_y, 
			  total_mobility_z, pressure, saturations)
Vector       *xvel;
Vector       *yvel;
Vector       *zvel;
ProblemData  *problem_data;
Vector       *total_mobility_x;
Vector       *total_mobility_y;
Vector       *total_mobility_z;
Vector       *pressure;
Vector      **saturations;
{
   PFModule      *this_module   = ThisPFModule;
   InstanceXtra  *instance_xtra = PFModuleInstanceXtra(this_module);
   PublicXtra    *public_xtra   = PFModulePublicXtra(this_module);

   PFModule       *phase_mobility      = (instance_xtra -> phase_mobility);
   PFModule       *capillary_pressure  = (instance_xtra -> capillary_pressure);
   PFModule       *phase_density       = (instance_xtra -> phase_density);

   Problem        *problem   = (instance_xtra -> problem);
   Grid           *grid    = (instance_xtra -> grid);
   Grid           *x_grid    = (instance_xtra -> x_grid);
   Grid           *y_grid    = (instance_xtra -> y_grid);
   Grid           *z_grid    = (instance_xtra -> z_grid);

   Vector         *temp_mobility_x = (instance_xtra -> temp_mobility_x);
   Vector         *temp_mobility_y = (instance_xtra -> temp_mobility_y);
   Vector         *temp_mobility_z = (instance_xtra -> temp_mobility_z);
   Vector         *temp_pressure = (instance_xtra -> temp_pressure);

   GrGeomSolid    *gr_domain     = ProblemDataGrDomain(problem_data);

   SubgridArray   *subgrids;
   Subgrid        *subgrid;

   Subvector      *subvector_p, *subvector_m, *subvector_t, *subvector_v;

   int             r;
   int             ix, iy, iz;
   int             nx, ny, nz;
   double          dx, dy, dz;

   int             nx_p, ny_p, nz_p;
   int             nx_m, ny_m, nz_m;
   int             nx_v, ny_v, nz_v;

   int             pi, mi, vi;

   int            *fdir;

   int             ipatch, sg, i, j, k, phase;
   int             flopest;

   double         *pl, *pu, *tl, *tu, *ml, *mu, *vel;
   double          dtmp, temp_density;

   CommHandle     *handle;

   Vector         *vel_vec[3];
   Subvector      *subvector_v0,
                  *subvector_v1,
                  *subvector_v2;
   double         *vel0_l, *vel0_r,
                  *vel1_l, *vel1_r,
                  *vel2_l, *vel2_r,
                  *vel_tmp;
   double          ds[3];
   double          h0, h1, h2, base_constant;
   int             dir0, dir1, dir2, alpha;
   int             dir[3][3] = {{1,0,0},{0,1,0},{0,0,1}};

   /*-----------------------------------------------------------------------
    * Begin timing
    *----------------------------------------------------------------------*/

   BeginTiming(public_xtra -> time_index);

   /************************************************************************
    *      First do the computations with the total mobility               *
    *           and the pressure for the base phase.                       *
    ************************************************************************/

   /*----------------------------------------------------------------------
    * exchange boundary data for pressure values
    *----------------------------------------------------------------------*/

   handle = InitVectorUpdate(pressure, VectorUpdateAll);
   FinalizeVectorUpdate(handle);

   /*----------------------------------------------------------------------
    * exchange boundary data for total mobility values
    *----------------------------------------------------------------------*/

   handle = InitVectorUpdate(total_mobility_x, VectorUpdateAll);
   FinalizeVectorUpdate(handle);
   handle = InitVectorUpdate(total_mobility_y, VectorUpdateAll);
   FinalizeVectorUpdate(handle);
   handle = InitVectorUpdate(total_mobility_z, VectorUpdateAll);
   FinalizeVectorUpdate(handle);

   /*----------------------------------------------------------------------
    * compute the x-face total velocities for each subgrid
    *----------------------------------------------------------------------*/

   subgrids = GridSubgrids(x_grid);
   ForSubgridI(sg, subgrids)
   {
      subgrid = SubgridArraySubgrid(subgrids, sg);

      subvector_p    = VectorSubvector(pressure, sg);
      subvector_t    = VectorSubvector(total_mobility_x, sg);
      subvector_v    = VectorSubvector(xvel,     sg);

      ix = SubgridIX(subgrid);
      iy = SubgridIY(subgrid) - 1;
      iz = SubgridIZ(subgrid) - 1;

      nx = SubgridNX(subgrid);
      ny = SubgridNY(subgrid) + 2;
      nz = SubgridNZ(subgrid) + 2;

      dx = SubgridDX(subgrid);
      dy = SubgridDY(subgrid);
      dz = SubgridDZ(subgrid);

      nx_p = SubvectorNX(subvector_p);
      ny_p = SubvectorNY(subvector_p);
      nz_p = SubvectorNZ(subvector_p);

      nx_m = SubvectorNX(subvector_t);
      ny_m = SubvectorNY(subvector_t);
      nz_m = SubvectorNZ(subvector_t);

      nx_v = SubvectorNX(subvector_v);
      ny_v = SubvectorNY(subvector_v);
      nz_v = SubvectorNZ(subvector_v);

      flopest = (FuncOps + 4) * nx_v * ny_v * nz_v;

      pl = SubvectorElt(subvector_p, ix-1,iy  ,iz  );
      pu = SubvectorElt(subvector_p, ix  ,iy  ,iz  );
				     
      ml = SubvectorElt(subvector_t, ix-1,iy  ,iz  );
      mu = SubvectorElt(subvector_t, ix  ,iy  ,iz  );

      vel = SubvectorElt(subvector_v, ix  ,iy  ,iz);

      pi = 0; mi = 0; vi = 0;

      BoxLoopI3(i,j,k,
                ix,iy,iz,nx,ny,nz,
                pi,nx_p,ny_p,nz_p,1,1,1,
                mi,nx_m,ny_m,nz_m,1,1,1,
                vi,nx_v,ny_v,nz_v,1,1,1,
      {
         vel[vi] = - Func( ml[mi], mu[mi] ) * (pu[pi] - pl[pi]) / dx;
      });

      IncFLOPCount( flopest );

   }

   /*----------------------------------------------------------------------
    * compute the y-face total velocities for each subgrid
    *----------------------------------------------------------------------*/

   subgrids = GridSubgrids(y_grid);
   ForSubgridI(sg, subgrids)
   {
      subgrid = SubgridArraySubgrid(subgrids, sg);

      subvector_p    = VectorSubvector(pressure, sg);
      subvector_t    = VectorSubvector(total_mobility_y, sg);
      subvector_v    = VectorSubvector(yvel,     sg);

      ix = SubgridIX(subgrid) - 1;
      iy = SubgridIY(subgrid);
      iz = SubgridIZ(subgrid) - 1;

      nx = SubgridNX(subgrid) + 2;
      ny = SubgridNY(subgrid);
      nz = SubgridNZ(subgrid) + 2;

      dx = SubgridDX(subgrid);
      dy = SubgridDY(subgrid);
      dz = SubgridDZ(subgrid);

      nx_p = SubvectorNX(subvector_p);
      ny_p = SubvectorNY(subvector_p);
      nz_p = SubvectorNZ(subvector_p);

      nx_m = SubvectorNX(subvector_t);
      ny_m = SubvectorNY(subvector_t);
      nz_m = SubvectorNZ(subvector_t);

      nx_v = SubvectorNX(subvector_v);
      ny_v = SubvectorNY(subvector_v);
      nz_v = SubvectorNZ(subvector_v);

      flopest = (FuncOps + 4) * nx_v * ny_v * nz_v;

      pl = SubvectorElt(subvector_p, ix  ,iy-1,iz  );
      pu = SubvectorElt(subvector_p, ix  ,iy  ,iz  );
				     
      ml = SubvectorElt(subvector_t, ix  ,iy-1,iz  );
      mu = SubvectorElt(subvector_t, ix  ,iy  ,iz  );

      vel = SubvectorElt(subvector_v, ix  ,iy  ,iz);

      mi = 0; pi = 0; vi = 0;

      BoxLoopI3(i,j,k,
                ix,iy,iz,nx,ny,nz,
                pi,nx_p,ny_p,nz_p,1,1,1,
                mi,nx_m,ny_m,nz_m,1,1,1,
                vi,nx_v,ny_v,nz_v,1,1,1,
      {
         vel[vi] = - Func( ml[mi], mu[mi] ) * (pu[pi] - pl[pi]) / dy;
       });

      IncFLOPCount( flopest );

   }

   /*----------------------------------------------------------------------
    * compute the z-face total velocities for each subgrid
    *----------------------------------------------------------------------*/

   subgrids = GridSubgrids(z_grid);
   ForSubgridI(sg, subgrids)
   {
      subgrid = SubgridArraySubgrid(subgrids, sg);

      subvector_p    = VectorSubvector(pressure, sg);
      subvector_t    = VectorSubvector(total_mobility_z, sg);
      subvector_v    = VectorSubvector(zvel,     sg);

      ix = SubgridIX(subgrid) - 1;
      iy = SubgridIY(subgrid) - 1;
      iz = SubgridIZ(subgrid);

      nx = SubgridNX(subgrid) + 2;
      ny = SubgridNY(subgrid) + 2;
      nz = SubgridNZ(subgrid);

      dx = SubgridDX(subgrid);
      dy = SubgridDY(subgrid);
      dz = SubgridDZ(subgrid);

      nx_p = SubvectorNX(subvector_p);
      ny_p = SubvectorNY(subvector_p);
      nz_p = SubvectorNZ(subvector_p);

      nx_m = SubvectorNX(subvector_t);
      ny_m = SubvectorNY(subvector_t);
      nz_m = SubvectorNZ(subvector_t);

      nx_v = SubvectorNX(subvector_v);
      ny_v = SubvectorNY(subvector_v);
      nz_v = SubvectorNZ(subvector_v);

      flopest = (FuncOps + 4) * nx_v * ny_v * nz_v;

      pl = SubvectorElt(subvector_p, ix  ,iy  ,iz-1);
      pu = SubvectorElt(subvector_p, ix  ,iy  ,iz  );
				     
      ml = SubvectorElt(subvector_t, ix  ,iy  ,iz-1);
      mu = SubvectorElt(subvector_t, ix  ,iy  ,iz  );

      vel = SubvectorElt(subvector_v, ix  ,iy  ,iz);

      mi = 0; pi = 0; vi = 0;

      BoxLoopI3(i,j,k,
                ix,iy,iz,nx,ny,nz,
                pi,nx_p,ny_p,nz_p,1,1,1,
                mi,nx_m,ny_m,nz_m,1,1,1,
                vi,nx_v,ny_v,nz_v,1,1,1,
      {
         vel[vi] = - Func( ml[mi], mu[mi] ) * (pu[pi] - pl[pi]) / dz;
      });

      IncFLOPCount( flopest );

   }

   /************************************************************************
    *      Now do the computations with the individual mobilities,         *
    *         capillary pressure, and densities for eash phase.            *
    ************************************************************************/

   for(phase = 0; phase < ProblemNumPhases(problem); phase++)
   {
      PFModuleInvoke(void, phase_mobility,
                     (temp_mobility_x, temp_mobility_y, temp_mobility_z,
		      ProblemDataPermeabilityX(problem_data),
		      ProblemDataPermeabilityY(problem_data),
		      ProblemDataPermeabilityZ(problem_data),
                      phase, saturations[phase],
                      ProblemPhaseViscosity(problem,phase)));

      handle = InitVectorUpdate(temp_mobility_x, VectorUpdateAll);
      FinalizeVectorUpdate(handle);
      handle = InitVectorUpdate(temp_mobility_y, VectorUpdateAll);
      FinalizeVectorUpdate(handle);
      handle = InitVectorUpdate(temp_mobility_z, VectorUpdateAll);
      FinalizeVectorUpdate(handle);
      
      PFModuleInvoke(void, capillary_pressure,
                     (temp_pressure, phase, 0, problem_data, saturations[0]));

      handle = InitVectorUpdate(temp_pressure, VectorUpdateAll);
      FinalizeVectorUpdate(handle);

      /*-------------------------------------------------------------------
       * add contributions to the x-face total velocities for each subgrid
       *-------------------------------------------------------------------*/

      subgrids = GridSubgrids(x_grid);
      ForSubgridI(sg, subgrids)
      {
         subgrid = SubgridArraySubgrid(subgrids, sg);

         subvector_p    = VectorSubvector(temp_pressure,    sg);
         subvector_t    = VectorSubvector(total_mobility_x, sg);
         subvector_m    = VectorSubvector(temp_mobility_x,  sg);
         subvector_v    = VectorSubvector(xvel,             sg);

         ix = SubgridIX(subgrid);
         iy = SubgridIY(subgrid) - 1;
         iz = SubgridIZ(subgrid) - 1;

         nx = SubgridNX(subgrid);
         ny = SubgridNY(subgrid) + 2;
         nz = SubgridNZ(subgrid) + 2;

         dx = SubgridDX(subgrid);
         dy = SubgridDY(subgrid);
         dz = SubgridDZ(subgrid);

         nx_p = SubvectorNX(subvector_p);
         ny_p = SubvectorNY(subvector_p);
         nz_p = SubvectorNZ(subvector_p);

         nx_m = SubvectorNX(subvector_m);
         ny_m = SubvectorNY(subvector_m);
         nz_m = SubvectorNZ(subvector_m);

         nx_v = SubvectorNX(subvector_v);
         ny_v = SubvectorNY(subvector_v);
         nz_v = SubvectorNZ(subvector_v);

         flopest = (CoeffOps + 4) * nx_v * ny_v * nz_v;

         pl = SubvectorElt(subvector_p, ix-1,iy  ,iz  );
         pu = SubvectorElt(subvector_p, ix  ,iy  ,iz  );
				        
         tl = SubvectorElt(subvector_t, ix-1,iy  ,iz  );
         tu = SubvectorElt(subvector_t, ix  ,iy  ,iz  );
				        
         ml = SubvectorElt(subvector_m, ix-1,iy  ,iz  );
         mu = SubvectorElt(subvector_m, ix  ,iy  ,iz  );

         vel = SubvectorElt(subvector_v, ix  ,iy  ,iz);

         pi = 0; mi = 0; vi = 0;

         BoxLoopI3(i,j,k,
                   ix,iy,iz,nx,ny,nz,
                   pi,nx_p,ny_p,nz_p,1,1,1,
                   mi,nx_m,ny_m,nz_m,1,1,1,
                   vi,nx_v,ny_v,nz_v,1,1,1,
         {
            vel[vi] -= Coeff( tl[mi], tu[mi], ml[mi], mu[mi] ) *
	       (pu[pi] - pl[pi]) / dx;
         });

         IncFLOPCount( flopest );

      }

      /*-------------------------------------------------------------------
       * add contributions to the y-face total velocities for each subgrid
       *-------------------------------------------------------------------*/

      subgrids = GridSubgrids(y_grid);
      ForSubgridI(sg, subgrids)
      {
         subgrid = SubgridArraySubgrid(subgrids, sg);

         subvector_p    = VectorSubvector(temp_pressure,    sg);
         subvector_t    = VectorSubvector(total_mobility_y, sg);
         subvector_m    = VectorSubvector(temp_mobility_y,  sg);
         subvector_v    = VectorSubvector(yvel,             sg);

         ix = SubgridIX(subgrid) - 1;
         iy = SubgridIY(subgrid);
         iz = SubgridIZ(subgrid) - 1;

         nx = SubgridNX(subgrid) + 2;
         ny = SubgridNY(subgrid);
         nz = SubgridNZ(subgrid) + 2;

         dx = SubgridDX(subgrid);
         dy = SubgridDY(subgrid);
         dz = SubgridDZ(subgrid);

         nx_p = SubvectorNX(subvector_p);
         ny_p = SubvectorNY(subvector_p);
         nz_p = SubvectorNZ(subvector_p);

         nx_m = SubvectorNX(subvector_m);
         ny_m = SubvectorNY(subvector_m);
         nz_m = SubvectorNZ(subvector_m);

         nx_v = SubvectorNX(subvector_v);
         ny_v = SubvectorNY(subvector_v);
         nz_v = SubvectorNZ(subvector_v);

         flopest = (CoeffOps + 4) * nx_v * ny_v * nz_v;

         pl = SubvectorElt(subvector_p, ix  ,iy-1,iz  );
         pu = SubvectorElt(subvector_p, ix  ,iy  ,iz  );
				        
         tl = SubvectorElt(subvector_t, ix  ,iy-1,iz  );
         tu = SubvectorElt(subvector_t, ix  ,iy  ,iz  );
				        
         ml = SubvectorElt(subvector_m, ix  ,iy-1,iz  );
         mu = SubvectorElt(subvector_m, ix  ,iy  ,iz  );

         vel = SubvectorElt(subvector_v, ix  ,iy  ,iz);

         pi = 0; mi = 0; vi = 0;

         BoxLoopI3(i,j,k,
                   ix,iy,iz,nx,ny,nz,
                   pi,nx_p,ny_p,nz_p,1,1,1,
                   mi,nx_m,ny_m,nz_m,1,1,1,
                   vi,nx_v,ny_v,nz_v,1,1,1,
         {
            vel[vi] -= Coeff( tl[mi], tu[mi], ml[mi], mu[mi] )
	       * (pu[pi] - pl[pi]) / dy;
         });

         IncFLOPCount( flopest );

      }

      /*-------------------------------------------------------------------
       * add contributions to the z-face total velocities for each subgrid
       *-------------------------------------------------------------------*/

      PFModuleInvoke(void, phase_density,
		     (phase, pressure, NULL, &dtmp, &temp_density, CALCFCN));

      base_constant = temp_density * ProblemGravity(problem);

      subgrids = GridSubgrids(z_grid);
      ForSubgridI(sg, subgrids)
      {
         subgrid = SubgridArraySubgrid(subgrids, sg);

         subvector_p    = VectorSubvector(temp_pressure,    sg);
         subvector_t    = VectorSubvector(total_mobility_z, sg);
         subvector_m    = VectorSubvector(temp_mobility_z,  sg);
         subvector_v    = VectorSubvector(zvel,             sg);

         ix = SubgridIX(subgrid) - 1;
         iy = SubgridIY(subgrid) - 1;
         iz = SubgridIZ(subgrid);

         nx = SubgridNX(subgrid) + 2;
         ny = SubgridNY(subgrid) + 2;
         nz = SubgridNZ(subgrid);

         dx = SubgridDX(subgrid);
         dy = SubgridDY(subgrid);
         dz = SubgridDZ(subgrid);

         nx_p = SubvectorNX(subvector_p);
         ny_p = SubvectorNY(subvector_p);
         nz_p = SubvectorNZ(subvector_p);

         nx_m = SubvectorNX(subvector_m);
         ny_m = SubvectorNY(subvector_m);
         nz_m = SubvectorNZ(subvector_m);

         nx_v = SubvectorNX(subvector_v);
         ny_v = SubvectorNY(subvector_v);
         nz_v = SubvectorNZ(subvector_v);

         flopest = (CoeffOps + 5) * nx_v * ny_v * nz_v;

         pl = SubvectorElt(subvector_p, ix  ,iy  ,iz-1);
         pu = SubvectorElt(subvector_p, ix  ,iy  ,iz  );
				        
         tl = SubvectorElt(subvector_t, ix  ,iy  ,iz-1);
         tu = SubvectorElt(subvector_t, ix  ,iy  ,iz  );
				        
         ml = SubvectorElt(subvector_m, ix  ,iy  ,iz-1);
         mu = SubvectorElt(subvector_m, ix  ,iy  ,iz  );

         vel = SubvectorElt(subvector_v, ix  ,iy  ,iz);

         pi = 0; mi = 0; vi = 0;

         BoxLoopI3(i,j,k,
                   ix,iy,iz,nx,ny,nz,
                   pi,nx_p,ny_p,nz_p,1,1,1,
                   mi,nx_m,ny_m,nz_m,1,1,1,
                   vi,nx_v,ny_v,nz_v,1,1,1,
         {
            vel[vi] -= Coeff( tl[mi], tu[mi], ml[mi], mu[mi] )
	       * ((pu[pi] - pl[pi]) / dz + base_constant);
         });

         IncFLOPCount( flopest );

      }
   }

   /*----------------------------------------------------------------------
    * Fixup the boundary values.
    *
    * CSW:  Previously, we applied a divergence free condition to the 
    * velocities and computed the boundary face velocities accordingly.
    * With the change to a single grid (f_grid and a_grid becoming one),
    * this was no longer an easy fix-up.  We now apply a zero boundary
    * velocity condition.
    * Below is the description of what was previously done...
    * --------------------------------------------------------------------
    * Use the div(V) = 0 constraint to put velocity values on the
    * boundary that couldn't be done in the previous code.
    *
    * In the following, the indices 0, 1, and 2 are used to represent
    * directions x, y, and z, respectively.  We use the integer variables
    * `dir0', `dir1', `dir2' to represent the "primary" direction
    * and two "secondary" directions.  Here "primary" essentially the
    * direction of interest or the direction which we are modifying.
    *----------------------------------------------------------------------*/

   vel_vec[0] = xvel;
   vel_vec[1] = yvel;
   vel_vec[2] = zvel;

   ForSubgridI(sg, GridSubgrids(grid))
   {
      subgrid = GridSubgrid(grid, sg);

      /* RDF: assume resolution is the same in all 3 directions */
      r  = SubgridRX(subgrid);

      ix = SubgridIX(subgrid);
      iy = SubgridIY(subgrid);
      iz = SubgridIZ(subgrid);

      nx = SubgridNX(subgrid);
      ny = SubgridNY(subgrid);
      nz = SubgridNZ(subgrid);

      ds[0] = SubgridDX(subgrid);
      ds[1] = SubgridDY(subgrid);
      ds[2] = SubgridDZ(subgrid);

      for (ipatch = 0; ipatch < GrGeomSolidNumPatches(gr_domain); ipatch++)
      {
         GrGeomPatchLoop(i, j, k, fdir, gr_domain, ipatch,
			 r, ix, iy, iz, nx, ny, nz,
	 {
	    /* primary direction x */
	    if (fdir[0])
	    {
	       dir0 = 0;
	       dir1 = 1;
	       dir2 = 2;
	    }
	    /* primary direction y */
	    if (fdir[1])
	    {
	       dir0 = 1;
	       dir1 = 0;
	       dir2 = 2;
	    }
	    /* primary direction z */
	    if (fdir[2])
	    {
	       dir0 = 2;
	       dir1 = 0;
	       dir2 = 1;
	    }
	    alpha = -fdir[dir0];
	    
	    subvector_v0   = VectorSubvector(vel_vec[dir0], sg);
	    subvector_v1   = VectorSubvector(vel_vec[dir1], sg);
	    subvector_v2   = VectorSubvector(vel_vec[dir2], sg);
	    
	    vel0_l = SubvectorElt(subvector_v0, i  ,j  ,k  );
	    vel0_r = SubvectorElt(subvector_v0,
				  i+dir[dir0][0],
				  j+dir[dir0][1],
				  k+dir[dir0][2] );
	    vel1_l = SubvectorElt(subvector_v1, i  ,j  ,k  );
	    vel1_r = SubvectorElt(subvector_v1,
				  i+dir[dir1][0],
				  j+dir[dir1][1],
				  k+dir[dir1][2] );
	    vel2_l = SubvectorElt(subvector_v2, i  ,j  ,k  );
	    vel2_r = SubvectorElt(subvector_v2,
				  i+dir[dir2][0],
				  j+dir[dir2][1],
				  k+dir[dir2][2] );
	    
	    if ( fdir[dir0] == -1 )
	    {
	       vel_tmp = vel0_r;
	       vel0_r  = vel0_l;
	       vel0_l  = vel_tmp;
	    }
	    
	    h0 = ds[dir0];
	    h1 = ds[dir1];
	    h2 = ds[dir2];
	    

	    /*	Apply a xero velocity condition on outer boundaries */
	    vel0_r[0] = 0.0;

	    /*
            vel0_r[0] = vel0_l[0]
	       + alpha*h0*( (vel1_r[0] - vel1_l[0])/h1
			   + (vel2_r[0] - vel2_l[0])/h2 );
	    */

         });
      }
   }

   /*----------------------------------------------------------------------
    * exchange boundary data for x-velocity values
    *----------------------------------------------------------------------*/

   handle = InitVectorUpdate(xvel, VectorUpdateAll);
   FinalizeVectorUpdate(handle);

   /*----------------------------------------------------------------------
    * exchange boundary data for y-velocity values
    *----------------------------------------------------------------------*/

   handle = InitVectorUpdate(yvel, VectorUpdateAll);
   FinalizeVectorUpdate(handle);

   /*----------------------------------------------------------------------
    * exchange boundary data for z-velocity values
    *----------------------------------------------------------------------*/

   handle = InitVectorUpdate(zvel, VectorUpdateVelZ);
   FinalizeVectorUpdate(handle);

   /*-----------------------------------------------------------------------
    * End timing
    *----------------------------------------------------------------------*/

    EndTiming(public_xtra -> time_index);
}


/*-------------------------------------------------------------------------
 * TotalVelocityFaceInitInstanceXtra
 *-------------------------------------------------------------------------*/

PFModule *TotalVelocityFaceInitInstanceXtra(problem, grid, x_grid, 
					    y_grid, z_grid, temp_data)
Problem  *problem;
Grid     *grid;
Grid     *x_grid;
Grid     *y_grid;
Grid     *z_grid;
double   *temp_data;
{
   PFModule     *this_module  = ThisPFModule;
   InstanceXtra *instance_xtra;

   if ( PFModuleInstanceXtra(this_module) == NULL )
      instance_xtra = ctalloc(InstanceXtra, 1);
   else
      instance_xtra = PFModuleInstanceXtra(this_module);

   /*----------------------------------------------------------------------
    * Setup the InstanceXtra structure
    *----------------------------------------------------------------------*/
   if ( problem != NULL )
   {
      (instance_xtra -> problem) = problem;
   }

   /*** Set the pointer to the grids ***/
   if ( grid != NULL )
   {
      /* free old data */
      if ( (instance_xtra -> grid) != NULL )
      {
         FreeTempVector(instance_xtra -> temp_mobility_x);
         FreeTempVector(instance_xtra -> temp_mobility_y);
         FreeTempVector(instance_xtra -> temp_mobility_z);
         FreeTempVector(instance_xtra -> temp_pressure);
      }

      /* set new data */
      (instance_xtra -> grid) = grid;

      (instance_xtra -> temp_mobility_x) = NewTempVector(grid, 1, 1);
      (instance_xtra -> temp_mobility_y) = NewTempVector(grid, 1, 1);
      (instance_xtra -> temp_mobility_z) = NewTempVector(grid, 1, 1);
      (instance_xtra -> temp_pressure) = NewTempVector(grid, 1, 1);
   }

   if ( x_grid != NULL )
   {
      (instance_xtra -> x_grid) = x_grid;
   }

   if ( y_grid != NULL )
   {
      (instance_xtra -> y_grid) = y_grid;
   }

   if ( z_grid != NULL )
   {
      (instance_xtra -> z_grid) = z_grid;
   }

   if ( temp_data != NULL )
   {
      (instance_xtra -> temp_data) = temp_data;

      SetTempVectorData((instance_xtra -> temp_mobility_x), temp_data);
      temp_data += SizeOfVector(instance_xtra -> temp_mobility_x);
      SetTempVectorData((instance_xtra -> temp_mobility_y), temp_data);
      temp_data += SizeOfVector(instance_xtra -> temp_mobility_y);
      SetTempVectorData((instance_xtra -> temp_mobility_z), temp_data);
      temp_data += SizeOfVector(instance_xtra -> temp_mobility_z);

      SetTempVectorData((instance_xtra -> temp_pressure), temp_data);
      temp_data += SizeOfVector(instance_xtra -> temp_pressure);
   }

   if ( PFModuleInstanceXtra(this_module) == NULL )
   {
      (instance_xtra -> phase_mobility) =
     PFModuleNewInstance(ProblemPhaseMobility(problem), ());
      (instance_xtra -> capillary_pressure) =
     PFModuleNewInstance(ProblemCapillaryPressure(problem), ());
      (instance_xtra -> phase_density) =
     PFModuleNewInstance(ProblemPhaseDensity(problem), ());
   }
   else
   {
      PFModuleReNewInstance((instance_xtra -> phase_mobility), ());
      PFModuleReNewInstance((instance_xtra -> capillary_pressure), ());
      PFModuleReNewInstance((instance_xtra -> phase_density), ());
   }

   PFModuleInstanceXtra(this_module) = instance_xtra;

   return this_module;
}

/*-------------------------------------------------------------------------
 * TotalVelocityFaceFreeInstanceXtra
 *-------------------------------------------------------------------------*/

void  TotalVelocityFaceFreeInstanceXtra()
{
   PFModule     *this_module   = ThisPFModule;
   InstanceXtra *instance_xtra = PFModuleInstanceXtra(this_module);

   if ( instance_xtra )
   {
      PFModuleFreeInstance(instance_xtra -> phase_mobility);
      PFModuleFreeInstance(instance_xtra -> capillary_pressure);
      PFModuleFreeInstance(instance_xtra -> phase_density);

      FreeTempVector(instance_xtra -> temp_mobility_x);
      FreeTempVector(instance_xtra -> temp_mobility_y);
      FreeTempVector(instance_xtra -> temp_mobility_z);
      FreeTempVector(instance_xtra -> temp_pressure);

      free( instance_xtra );
   }
}

/*-------------------------------------------------------------------------
 * TotalVelocityFaceNewPublicXtra
 *-------------------------------------------------------------------------*/

PFModule  *TotalVelocityFaceNewPublicXtra()
{
   PFModule     *this_module  = ThisPFModule;
   PublicXtra   *public_xtra;

   /*----------------------------------------------------------------------
    * Setup the PublicXtra structure
    *----------------------------------------------------------------------*/
   public_xtra = ctalloc(PublicXtra, 1);

   /*-------------------------------------------------------------*/
   /*                receive user input parameters                */

   /*-------------------------------------------------------------*/
   /*                     setup parameters                        */

   (public_xtra -> time_index) = RegisterTiming("Total Velocity Face");

   /*-------------------------------------------------------------*/

   PFModulePublicXtra(this_module) = public_xtra;

   return this_module;
}

/*-------------------------------------------------------------------------
 * TotalVelocityFaceFreePublicXtra
 *-------------------------------------------------------------------------*/

void TotalVelocityFaceFreePublicXtra()
{
   PFModule     *this_module  = ThisPFModule;
   PublicXtra   *public_xtra  = PFModulePublicXtra(this_module);

   if ( public_xtra )
   {
      free( public_xtra );
   }

}


/*-------------------------------------------------------------------------
 * TotalVelocityFaceSizeOfTempData
 *-------------------------------------------------------------------------*/

int  TotalVelocityFaceSizeOfTempData()
{
   PFModule      *this_module   = ThisPFModule;
   InstanceXtra  *instance_xtra   = PFModuleInstanceXtra(this_module);

   int  sz = 0;

   /* add local TempData size to `sz' */
   sz += SizeOfVector(instance_xtra -> temp_mobility_x);
   sz += SizeOfVector(instance_xtra -> temp_mobility_y);
   sz += SizeOfVector(instance_xtra -> temp_mobility_z);
   sz += SizeOfVector(instance_xtra -> temp_pressure);

   return sz;
}