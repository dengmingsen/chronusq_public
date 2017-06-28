/* 
 *  This file is part of the Chronus Quantum (ChronusQ) software package
 *  
 *  Copyright (C) 2014-2017 Li Research Group (University of Washington)
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *  
 *  Contact the Developers:
 *    E-Mail: xsli@uw.edu
 *  
 */

#include <aointegrals.hpp>
#include <cqlinalg.hpp>

// Debug directives
//#define _DEBUGORTHO

namespace ChronusQ {

  typedef std::vector<libint2::Shell> shell_set; 

  /**
   *  \brief A general wrapper for 1-e (2 index) integral evaluation.
   *
   *  Currently computes 1-e integrals using Libint2. Shells sets are
   *  passed in order to be possibly general to the uncontracted basis.
   *  Handles all internal memory allocation including the evaluated matricies
   *  themselves
   *
   *  \param [in] op     Operator for which to calculate the 1-e integrals
   *  \param [in] shells Shell set for the integral evaluation
   *
   *  \returns    A vector of properly allocated pointers which store the
   *              1-e evaluations.
   *
   *  This function returns a vector of pointers as it sometimes makes sense
   *  to evaluate several matricies together if they are inimately related,
   *  namely the length gauge electric multipoles and the overlap.
   *
   *  z.B. op == libint2::Operator::emultipole3
   *
   *  The function will return a vector of 20 pointers in the following order
   *  { overlap, 
   *    dipole_x, dipole_y, dipole_z, 
   *    quadrupole_xx, quadrupole_xy, quadrupole_xz, quadrupole_yy,
   *      quadrupole_yz, quadrupole_zz,
   *    octupole_xxx, octupole_xxy, octupole_xxz, octupole_xyy,
   *      octupole_xyz, octupole_xzz, octupole_yyy, octupole_yyz,
   *      octupole_yzz, octupole_zzz
   *  }
   *
   *  z.B. op == libint2::Operator::kinetic
   *
   *  The function will return a vector of 1 pointer
   *
   *  { kinetic }
   */ 
  AOIntegrals::oper_t_coll AOIntegrals::OneEDriver(libint2::Operator op, 
    shell_set& shells) {



    // Determine the number of basis functions for the passed shell set
    size_t NB = std::accumulate(shells.begin(),shells.end(),0,
      [](size_t init, libint2::Shell &sh) -> size_t {
        return init + sh.size();
      }
    );

    size_t NBSQ = NB*NB;


    // Determine the maximum angular momentum of the passed shell set
    int maxL = std::max_element(shells.begin(), shells.end(),
      [](libint2::Shell &sh1, libint2::Shell &sh2){
        return sh1.contr[0].l < sh2.contr[0].l;
      }
    )->contr[0].l;

    // Determine the maximum contraction depth of the passed shell set
    int maxPrim = std::max_element(shells.begin(), shells.end(),
      [](libint2::Shell &sh1, libint2::Shell &sh2){
        return sh1.alpha.size() < sh2.alpha.size();
      }
    )->alpha.size();

    // Determine the number of OpenMP threads
#ifdef _OPENMP
    int nthreads = omp_get_max_threads();
#else
    int nthreads = 1;
#endif


    // Create a vector of libint2::Engines for possible threading
    std::vector<libint2::Engine> engines(nthreads);

    // Initialize the first engine for the integral evaluation
    engines[0] = libint2::Engine(op,maxPrim,maxL,0);
    engines[0].set_precision(0.0);


    // If engine is V, define nuclear charges
    if(op == libint2::Operator::nuclear){
      std::vector<std::pair<double,std::array<double,3>>> q;
      for(auto &atom : molecule_.atoms)
        q.push_back( { static_cast<double>(atom.atomicNumber), atom.coord } );

      engines[0].set_params(q);
    }

    // Copy over the engines to other threads if need be
    for(size_t i = 1; i < nthreads; i++) engines[i] = engines[0];


    // Determine the number of operators
    AOIntegrals::oper_t_coll mats( engines[0].results().size() );

    std::vector<
      Eigen::Map<
        Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic,Eigen::ColMajor>
      > 
    > matMaps;
    for( auto i = 0; i < mats.size(); i++ ) {
      mats[i] = memManager_.malloc<double>(NBSQ);
      std::fill_n(mats[i],NBSQ,0.);
      matMaps.emplace_back(mats[i],NB,NB);
    }

    #pragma omp parallel
    {
#ifdef _OPENMP
      int thread_id = omp_get_thread_num();
#else
      int thread_id = 0;
#endif
      const auto& buf_vec = engines[thread_id].results();
      size_t n1,n2;

      // Loop over unique shell pairs
      for(size_t s1(0), bf1_s(0), s12(0); s1 < shells.size(); bf1_s+=n1, s1++){ 
        n1 = shells[s1].size();
      for(size_t s2(0), bf2_s(0); s2 <= s1; bf2_s+=n2, s2++, s12++) {
        n2 = shells[s2].size();

        // Compute the integrals       
        engines[thread_id].compute(shells[s1],shells[s2]);

        // If the integrals were screened, move on to the next batch
        if(buf_vec[0] == nullptr) continue;

        // Place integral blocks into their respective matricies
        // XXX: USES EIGEN
        for(auto iMat = 0; iMat < buf_vec.size(); iMat++){
          Eigen::Map<
            const Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic,
              Eigen::RowMajor>>
            bufMat(buf_vec[iMat],n1,n2);

          matMaps[iMat].block(bf1_s,bf2_s,n1,n2) = bufMat;
        }

      } // Loop over s2 <= s1
      } // Loop over s1

    } // end OpenMP context


    // Symmetrize the matricies 
    // XXX: USES EIGEN
    for(auto nMat = 0; nMat < matMaps.size(); nMat++) 
      matMaps[nMat] = matMaps[nMat].selfadjointView<Eigen::Lower>();

    return mats;

  }; // AOIntegrals::OneEDriver


  /**
   *  \brief Allocate, compute  and store the 1-e integrals + 
   *  orthonormalization matricies over the given CGTO basis.
   *
   *  Computes:
   *    Overlap + length gauge Electric Multipoles
   *    Kinetic energy matrix
   *    Nuclear potential energy matrix
   *    Core Hamiltonian (T + V)
   *    Orthonormalization matricies (Lowdin / Cholesky)
   *
   *  TODO: Make this function general to relativistic Hamiltonians
   *
   */ 
  void AOIntegrals::computeAOOneE() {

    // Compute base 1-e integrals
    auto _multipole = 
      OneEDriver(libint2::Operator::emultipole3,basisSet_.shells);

    auto _kinetic = OneEDriver(libint2::Operator::kinetic,basisSet_.shells);
    auto _potential = OneEDriver(libint2::Operator::nuclear,basisSet_.shells);

    // Extract the pointers
    overlap = _multipole[0];
    std::copy_n(_multipole.begin()+1, 3, std::back_inserter(lenElecDipole));
    std::copy_n(_multipole.begin()+4, 6, std::back_inserter(lenElecQuadrupole));
    std::copy_n(_multipole.begin()+10,10,std::back_inserter(lenElecOctupole));

    kinetic   = _kinetic[0];
    potential = _potential[0];

    // Allocate and compute the core Hamiltonian
    coreH.emplace_back(memManager_.malloc<double>(nSQ_));
    std::fill_n(coreH.back(),nSQ_,0.);

    // Do the add in chunks in parallel to improve cache utilization
    size_t chunk = 2*nSQ_ 
    #ifdef _OPENMP
      / omp_get_max_threads()
    #endif
    ;

    // H = T + V
    #pragma omp parallel for default(shared) \
       schedule(static,chunk)
      for(size_t i = 0; i < nSQ_; i++) 
        coreH.back()[i] = kinetic[i] + potential[i];




    // Compute Orthonormalization trasformations
    computeOrtho();


  }; // AOIntegrals::computeAOOneE


  /**
   *  \brief Allocate, compute and store the orthonormalization matricies 
   *  over the CGTO basis.
   *
   *  Computes either the Lowdin or Cholesky transformation matricies based
   *  on AOIntegrals::orthoType_
   */ 
  void AOIntegrals::computeOrtho() {

    // Allocate orthogonalization matricies
    ortho1 = memManager_.malloc<double>(nSQ_);
    ortho2 = memManager_.malloc<double>(nSQ_);

    std::fill_n(ortho1,nSQ_,0.);
    std::fill_n(ortho2,nSQ_,0.);


    // Allocate scratch
    double* SCR1 = memManager_.malloc<double>(nSQ_);

    // Copy the overlap over to scratch space
    std::copy_n(overlap,nSQ_,SCR1);

    orthoType_ = CHOLESKY;

    if(orthoType_ == LOWDIN) {

      // Allocate more scratch
      double* sE   = memManager_.malloc<double>(basisSet_.nBasis);
      double* SCR2 = memManager_.malloc<double>(nSQ_);

      
      // Diagonalize the overlap in scratch S = V * s * V**T
      HermetianEigen('V','U',basisSet_.nBasis,SCR1,basisSet_.nBasis,
        sE,memManager_);

      // Compute X = V * s^{-1/2} 
      for(auto j = 0; j < basisSet_.nBasis; j++)
      for(auto i = 0; i < basisSet_.nBasis; i++)
        SCR2[i + j*basisSet_.nBasis] = 
          SCR1[i + j*basisSet_.nBasis] / std::sqrt(sE[j]);

      // Compute O1 = X * V**T
      Gemm('N','T',basisSet_.nBasis,basisSet_.nBasis,basisSet_.nBasis,
        1.,SCR2,basisSet_.nBasis,SCR1,basisSet_.nBasis,0.,ortho1,
        basisSet_.nBasis);

      // Compute X = V * s^{1/2} in place (by multiplying by s)
      for(auto j = 0; j < basisSet_.nBasis; j++)
      for(auto i = 0; i < basisSet_.nBasis; i++)
        SCR2[i + j*basisSet_.nBasis] = 
          SCR2[i + j*basisSet_.nBasis] * sE[j];

      // Compute O2 = X * V**T
      Gemm('N','T',basisSet_.nBasis,basisSet_.nBasis,basisSet_.nBasis,
        1.,SCR2,basisSet_.nBasis,SCR1,basisSet_.nBasis,0.,ortho2,
        basisSet_.nBasis);

      // Debug code to validate the Lowdin orthogonalization
#ifdef _DEBUGORTHO

      std::cerr << "Debugging Lowdin Orthogonalization" << std::endl;
      bool good(true);

      // Check that ortho1 and ortho2 are inverses of eachother
      good = true;
      Gemm('N','N',basisSet_.nBasis,basisSet_.nBasis,basisSet_.nBasis,
        1.,ortho1,basisSet_.nBasis,ortho2,basisSet_.nBasis,0.,SCR1,
        basisSet_.nBasis);
      
      for(auto j = 0; j < basisSet_.nBasis; j++)
      for(auto i = 0; i < basisSet_.nBasis; i++) {

        if( i == j ) good = good and
          (1. - SCR1[i + j*basisSet_.nBasis]) < 1e-12;
        else good = good and SCR1[i + j*basisSet_.nBasis] < 1e-12; 

      }

      std::cerr << "  Ortho1 * Ortho2 = I: " << std::boolalpha << good 
                << std::endl;

      // Check that ortho2 * ortho2 is the overlap
      good = true;
      Gemm('N','N',basisSet_.nBasis,basisSet_.nBasis,basisSet_.nBasis,
        1.,ortho2,basisSet_.nBasis,ortho2,basisSet_.nBasis,0.,SCR1,
        basisSet_.nBasis);
      
      for(auto j = 0; j < basisSet_.nBasis; j++)
      for(auto i = 0; i < basisSet_.nBasis; i++) {

        good = good and 
          (SCR1[i + j*basisSet_.nBasis] - overlap[i + j*basisSet_.nBasis]) <
          1e-12; 

      }

      std::cerr << "  Ortho2 * Ortho2 = S: " << std::boolalpha << good 
                << std::endl;

      // Check that ortho1 * ortho1 is the inverse of the overlap
      good = true;
      Gemm('N','N',basisSet_.nBasis,basisSet_.nBasis,basisSet_.nBasis,
        1.,ortho1,basisSet_.nBasis,ortho1,basisSet_.nBasis,0.,SCR1,
        basisSet_.nBasis);
      Gemm('N','N',basisSet_.nBasis,basisSet_.nBasis,basisSet_.nBasis,
        1.,SCR1,basisSet_.nBasis,overlap,basisSet_.nBasis,0.,SCR2,
        basisSet_.nBasis);
      
      for(auto j = 0; j < basisSet_.nBasis; j++)
      for(auto i = 0; i < basisSet_.nBasis; i++) {

        if( i == j ) good = good and
          (1. - SCR2[i + j*basisSet_.nBasis]) < 1e-10;
        else good = good and SCR2[i + j*basisSet_.nBasis] < 1e-10; 

      }

      std::cerr << "  Ortho1 * Ortho1 * S = I: " << std::boolalpha << good 
                << std::endl;

#endif

      // Free Scratch Space
      memManager_.free(sE,SCR2);

    } else if(orthoType_ == CHOLESKY) {

      // Compute the Cholesky factorization of the overlap S = L * L**T
      Cholesky('L',basisSet_.nBasis,SCR1,basisSet_.nBasis);

      // Copy the lower triangle to ortho2 (O2 = L)
      for(auto j = 0; j < basisSet_.nBasis; j++)
      for(auto i = j; i < basisSet_.nBasis; i++)
        ortho2[i + j*basisSet_.nBasis] = SCR1[i + j*basisSet_.nBasis];

      // Compute the inverse of the overlap using the Cholesky factors
      CholeskyInv('L',basisSet_.nBasis,SCR1,basisSet_.nBasis);

      // O1 = O2**T * S^{-1}
      Gemm('T','N',basisSet_.nBasis,basisSet_.nBasis,basisSet_.nBasis,
        1.,ortho2,basisSet_.nBasis,SCR1,basisSet_.nBasis,0.,ortho1,
        basisSet_.nBasis);

      // Remove upper triangle junk from O1
      for(auto j = 0; j < basisSet_.nBasis; j++)
      for(auto i = 0; i < j               ; i++)
        ortho1[i + j*basisSet_.nBasis] = 0.;

    }

    memManager_.free(SCR1);

  }; // AOIntegrals::computeOrtho

}; // namespace ChronusQ
