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
#ifndef __INCLUDED_AOINTEGRALS_CONTRACT_DIRECT_HPP__
#define __INCLUDED_AOINTEGRALS_CONTRACT_DIRECT_HPP__


#include <aointegrals.hpp>
#include <util/matout.hpp>
#include <cqlinalg/blas1.hpp>
#include <cqlinalg/blasext.hpp>
#include <cqlinalg/blasutil.hpp>

#include <util/threads.hpp>

#define _FULL_DIRECT
//#define _SUB_TIMINGS

#define _SHZ_SCREEN

#ifndef _FULL_DIRECT
  #define _BATCH_DIRECT
#endif

#if defined(_FULL_DIRECT) 
  #define _USE_EIGHT_FOLD
#else
  #define _USE_FOUR_FOLD
#endif

#ifndef _FULL_DIRECT
  #warning "Batch Direct ERI contraction is broken for complex"
#endif

#define GetRealPtr(X,I,J,N) reinterpret_cast<double*>(X + I + J*N)

namespace ChronusQ {

  /**
   *  \brief Handle the fact that std::conj actually returns 
   *  std::complex
   */
  template <typename T>
  inline T SmartConj(const T&);
 
  template <>
  inline double SmartConj(const double &x) { return x; }
  template <>
  inline dcomplex SmartConj(const dcomplex &x) { return std::conj(x); }



  /**
   *  \brief Perform various tensor contractions of the ERI tensor
   *  directly. Wraps other helper functions and provides
   *  loop structure
   *
   *  Currently supports
   *    - Coulomb-type (34,12) contractions
   *    - Exchange-type (23,12) contractions
   *
   *  Works with both real and complex matricies
   *
   *  \param [in/out] list Contains information pertinent to the 
   *    matricies to be contracted with. See TwoBodyContraction
   *    for details
   */ 
  template <typename T, typename G>
  void AOIntegrals::twoBodyContractDirect(
    std::vector<TwoBodyContraction<T,G>> &list) {

    directScaffold(list);
    
  }; // AOIntegrals::twoBodyContractDirect



  template <typename T>
  void ShellBlockNorm(std::vector<libint2::Shell> &shSet, T *MAT, 
    size_t LDM, double *ShBlk) {

    size_t nShell = shSet.size();

    size_t n1,n2;
    for(auto s1(0ul), bf1(0ul); s1 < nShell; s1++, bf1 += n1) {
      n1 = shSet[s1].size();
    for(auto s2(0ul), bf2(0ul); s2 < nShell; s2++, bf2 += n2) {
      n2 = shSet[s2].size();

      T *block = MAT + bf1 + bf2*LDM;
      ShBlk[s1 + s2*nShell] = MatNorm<double>('I',n1,n2,block,LDM);

    }
    }

  };


  template <typename T>
  double * ShellBlockNorm(std::vector<libint2::Shell> &shSet, T *MAT, 
    size_t LDM, CQMemManager &mem) {

    size_t nShell = shSet.size();
    double *ShBlk = mem.template malloc<double>(nShell*nShell);

    ShellBlockNorm(shSet,MAT,LDM,ShBlk);

    return ShBlk;

  };



  template <typename T, typename G>
  void AOIntegrals::directScaffold(
    std::vector<TwoBodyContraction<T,G>> &list) {

    size_t nthreads  = GetNumThreads();
    size_t LAThreads = GetLAThreads();

    SetLAThreads(1); // Turn off parallelism in LA functions

    const size_t NB   = basisSet_.nBasis;
    const size_t NMat = list.size();
    const size_t NS   = basisSet_.nShell;

#ifdef _SHZ_SCREEN
    // Check whether any of the contractions are non-hermetian
    const bool AnyNonHer = std::any_of(list.begin(),list.end(),
      []( TwoBodyContraction<T,G> & x ) -> bool { return not x.HER; });

    // Compute schwartz bounds if we haven't already
    if(schwartz == nullptr) computeSchwartz();
#endif




    // Create thread-safe libint2::Engine's
      
    std::vector<libint2::Engine> engines(nthreads);

    // Construct engine for master thread
    engines[0] = libint2::Engine(libint2::Operator::coulomb,basisSet_.maxPrim,
      basisSet_.maxL,0);





    // Allocate scratch for raw integral batches
    size_t maxShellSize = 
      std::max_element(basisSet_.shells.begin(),basisSet_.shells.end(),
        [](libint2::Shell &sh1, libint2::Shell &sh2) {
          return sh1.size() < sh2.size();
        })->size();

    size_t lenIntBuffer = 
      maxShellSize * maxShellSize * nSQ_; 

    lenIntBuffer *= 
      std::max(sizeof(T),sizeof(G)) / sizeof(double);

    size_t nBuffer = 2;
    double * intBuffer = 
      memManager_.malloc<double>(nBuffer*lenIntBuffer*nthreads);
   
    double *intBuffer2 = intBuffer + nthreads*lenIntBuffer;


    // Allocate thread local storage to store integral contractions
    // XXX: Don't allocate anything if serial
    std::vector<std::vector<G*>> AXthreads;
    G *AXRaw = nullptr;
    if(nthreads != 1) {
      AXRaw = memManager_.malloc<G>(nthreads*NMat*NB*NB);    
      memset(AXRaw,0,nthreads*NMat*NB*NB*sizeof(G));
    }

    for(auto ithread = 0, iMat = 0; ithread < nthreads; ithread++) {
      AXthreads.emplace_back();
    for(auto jMat = 0; jMat < NMat; jMat++, iMat++) {
      if(nthreads == 1) {
        AXthreads.back().push_back(list[jMat].AX);
      } else {
        AXthreads.back().push_back(AXRaw + iMat*NB*NB);
      }
    }
    }


#ifdef _SHZ_SCREEN
    // Compute shell block norms
    double *ShBlkNorms_raw = 
      memManager_.malloc<double>(list.size()*NS*NS);

    std::vector<double*> ShBlkNorms;
    for(auto iMat = 0, iOff = 0; iMat < NMat; iMat++, 
      iOff += NS*NS ) {

      ShellBlockNorm(basisSet_.shells,list[iMat].X,NB,
        ShBlkNorms_raw + iOff);

      ShBlkNorms.emplace_back(ShBlkNorms_raw + iOff);

    }

    double maxShBlk = 0.;
    for(auto iMat = 0; iMat < NMat; iMat++)
      maxShBlk = std::max(maxShBlk,
        *std::max_element(ShBlkNorms[iMat],ShBlkNorms[iMat] + NS*NS) ); 


    size_t NP4 = 
      basisSet_.maxPrim * basisSet_.maxPrim * basisSet_.maxPrim * 
      basisSet_.maxPrim;

    engines[0].set_precision(
      std::min(
        std::numeric_limits<double>::epsilon(),
        threshSchwartz / maxShBlk
      ) / NP4);
#else
    // Set precision
    engines[0].set_precision(std::numeric_limits<double>::epsilon());
#endif


    // Copy master thread engine to other threads
    for(size_t i = 1; i < nthreads; i++) engines[i] = engines[0];


#ifdef _SUB_TIMINGS
    std::chrono::duration<double> durInner(0.), durCont(0.), durSymm(0.),
      durZero(0.);
#endif

    // Keeping track of number of integrals skipped
    std::vector<size_t> nSkip(nthreads,0);


    auto topDirect = std::chrono::high_resolution_clock::now();
    #pragma omp parallel
    {

    // Set up thread local storage

    size_t thread_id = GetThreadID();

    auto &engine = engines[thread_id];
    const auto& buf_vec = engine.results();
    
    auto &AX_loc = AXthreads[thread_id];


    double * intBuffer_loc  = intBuffer  + thread_id*lenIntBuffer;
    double * intBuffer2_loc = intBuffer2 + thread_id*lenIntBuffer;


    size_t n1,n2;

    // Always Loop over s2 <= s1
    for(size_t s1(0ul), bf1_s(0ul), s12(0ul); s1 < NS; bf1_s+=n1, s1++) { 
      n1 = basisSet_.shells[s1].size(); // Size of Shell 1

    for(size_t s2(0ul), bf2_s(0ul); s2 <= s1; bf2_s+=n2, s2++, s12++) {
      n2 = basisSet_.shells[s2].size(); // Size of Shell 2


      // Round-Robbin work distribution
      if( s12 % nthreads != thread_id ) continue;


      // Cache variables for shells 1 and 2
        
#ifdef _FULL_DIRECT
      // Deneneracy factor for s1,s2 pair
      double s12_deg = (s1 == s2) ? 1.0 : 2.0;
#endif

#ifdef _SHZ_SCREEN
      double shz12 = schwartz[s1 + s2*NS];

      double shMax12 = ShBlkNorms[0][s1 + s2*NS];
      for(auto iMat = 1; iMat < NMat; iMat++)
        shMax12 = std::max(shMax12,ShBlkNorms[iMat][s1 + s2*NS]);

      // Check the opposite triangle
      if( AnyNonHer and s1 != s2 )
        for(auto iMat = 0; iMat < NMat; iMat++)
          shMax12 = std::max(shMax12,ShBlkNorms[iMat][s2 + s1*NS]);
#endif



#ifdef _BATCH_DIRECT

#ifdef _SUB_TIMINGS
      auto topZero = std::chrono::high_resolution_clock::now();
#endif

      // Zero out the integral buffer (hot spot)
      memset(intBuffer_loc,0,lenIntBuffer);

#ifdef _SUB_TIMINGS
      auto botZero = std::chrono::high_resolution_clock::now();
      durZero += botZero - topZero;
#endif

      double *intBuffCur = intBuffer_loc;

#endif


#ifdef _SUB_TIMINGS
      auto topInner = std::chrono::high_resolution_clock::now();
#endif


// The upper bound of s3 is s1 for the 8-fold symmetry and
// nShell for 4-fold.
#ifdef _USE_EIGHT_FOLD
  #define S3_MAX s1
#elif defined(_USE_FOUR_FOLD)
  // the "-" is for the <= in the loop
  #define S3_MAX NS - 1
#endif


      size_t n3,n4;
      for(size_t s3(0), bf3_s(0); s3 <= S3_MAX; s3++, bf3_s += n3) { 
        n3 = basisSet_.shells[s3].size(); // Size of Shell 3

#ifdef _SHZ_SCREEN
        // Pre-calculate shell-block norm max's that only
        // depend on shells 1,2 and 3
        double shMax123 = 
          std::max(ShBlkNorms[0][s1 + s3*NS], 
                   ShBlkNorms[0][s2 + s3*NS]);

        for(auto iMat = 1ul; iMat < NMat; iMat++){
          shMax123 = std::max(shMax123,
            std::max(ShBlkNorms[iMat][s1 + s3*NS], 
                     ShBlkNorms[iMat][s2 + s3*NS]));
        };

        // Check the opposite triangle
        if( AnyNonHer )
          for(auto iMat = 0ul; iMat < NMat; iMat++){
            shMax123 = std::max(shMax123,
              std::max(ShBlkNorms[iMat][s3 + s1*NS], 
                       ShBlkNorms[iMat][s3 + s2*NS]));
          };

        shMax123 = std::max(shMax123,shMax12);
#endif
        
// The upper bound of s4 is either s2 or s3 based on s1 and s3 for
// the 8-fold symmetry and s3 for the 4-fold symmetry
#ifdef _USE_EIGHT_FOLD
        size_t s4_max = (s1 == s3) ? s2 : s3;
#elif defined(_USE_FOUR_FOLD)
        size_t s4_max =  s3;
#endif


// If we're doing batch direct, also increment the current buffer index
#ifdef _BATCH_DIRECT
      for(size_t s4(0), bf4_s(0); s4 <= s4_max; s4++, bf4_s += n4,
        intBuffCur += n1*n2*n3*n4)
#else
      for(size_t s4(0), bf4_s(0); s4 <= s4_max; s4++, bf4_s += n4)
#endif
      {
        n4 = basisSet_.shells[s4].size(); // Size of Shell 4

#ifdef _SHZ_SCREEN
        // Compute Shell norm max
        double shMax = 
          std::max(ShBlkNorms[0][s1 + s4*NS],
          std::max(ShBlkNorms[0][s2 + s4*NS],
                   ShBlkNorms[0][s3 + s4*NS]));

        for(auto iMat = 1ul; iMat < NMat; iMat++){
          shMax = std::max(shMax,
            std::max(ShBlkNorms[iMat][s1 + s4*NS],
            std::max(ShBlkNorms[iMat][s2 + s4*NS],
                     ShBlkNorms[iMat][s3 + s4*NS])));
        };

        // Check the opposite triangle
        if( AnyNonHer )
          for(auto iMat = 0ul; iMat < NMat; iMat++){
            shMax = std::max(shMax,
              std::max(ShBlkNorms[iMat][s4 + s1*NS],
              std::max(ShBlkNorms[iMat][s4 + s2*NS],
                       ShBlkNorms[iMat][s4 + s3*NS])));
          };

        shMax = std::max(shMax,shMax123);

        if((shMax * shz12 * schwartz[s3 + s4*NS]) < 
           threshSchwartz) { nSkip[thread_id]++; continue; }
#endif
      

#ifdef _FULL_DIRECT

        // Degeneracy factor for s3,s4 pair
        double s34_deg = (s3 == s4) ? 1.0 : 2.0;

        // Degeneracy factor for s1, s2, s3, s4 quartet
        double s12_34_deg = (s1 == s3) ? (s2 == s4 ? 1.0 : 2.0) : 2.0;

        // Total degeneracy factor
        double s1234_deg = s12_deg * s34_deg * s12_34_deg;

#endif

        // Evaluate ERI for shell quartet (s1 s2 | s3 s4)
        engine.compute2<
          libint2::Operator::coulomb, libint2::BraKet::xx_xx, 0>(
          basisSet_.shells[s1],
          basisSet_.shells[s2],
          basisSet_.shells[s3],
          basisSet_.shells[s4]
        );

        // Libint2 internal screening
        const double *buff = buf_vec[0];
        if(buff == nullptr) { nSkip[thread_id]++; continue; }

#ifdef _BATCH_DIRECT

        // Copy over buffer
        //std::copy_n(buff,n1*n2*n3*n4,intBuffCur);
        memcpy(intBuffCur,buff,n1*n2*n3*n4*sizeof(double));

#elif defined(_FULL_DIRECT)

// Flag to turn contraction on and off
#if 1
        // Scale the buffer by the degeneracy factor and store
        // in infBuffer
        std::transform(buff,buff + n1*n2*n3*n4,intBuffer_loc,
          std::bind1st(std::multiplies<double>(),0.5*s1234_deg));

        size_t b1,b2,b3,b4;
        double *Xp1, *Xp2;
        double X1,X2;
        T      T1,T2,T3,T4;
        T      *Tp1,*Tp2;

        for(auto iMat = 0; iMat < NMat; iMat++) {
          
          // Hermetian contraction
          if( list[iMat].HER ) { 

            if( list[iMat].contType == COULOMB )
            for(auto i = 0ul, bf1 = bf1_s, ijkl(0ul); i < n1; i++, bf1++)      
            for(auto j = 0ul, bf2 = bf2_s; j < n2; j++, bf2++) { 
              // Cache i,j variables
              b1 = bf1 + NB*bf2; 
              X1 = *reinterpret_cast<double*>(list[iMat].X  + b1);
              Xp1 = reinterpret_cast<double*>(AX_loc[iMat] + b1);
            for(auto k = 0ul, bf3 = bf3_s; k < n3; k++, bf3++) 
            for(auto l = 0ul, bf4 = bf4_s; l < n4; l++, bf4++, ijkl++) { 

              // J(1,2) += I * X(4,3)
              *Xp1 += *GetRealPtr(list[iMat].X,bf4,bf3,NB) * intBuffer_loc[ijkl];

              // J(4,3) += I * X(1,2)
              *GetRealPtr(AX_loc[iMat],bf4,bf3,NB) +=  X1 * intBuffer_loc[ijkl];

              // J(2,1) and J(3,4) are handled on symmetrization after
              // contraction
                
            } // kl loop
            } // ij loop

            else if( list[iMat].contType == EXCHANGE )
            for(auto i = 0ul, bf1 = bf1_s, ijkl(0ul); i < n1; i++, bf1++)      
            for(auto j = 0ul, bf2 = bf2_s; j < n2; j++, bf2++)       
            for(auto k = 0ul, bf3 = bf3_s; k < n3; k++, bf3++) {

              // Cache i,j,k variables
              b1 = bf1 + bf3*NB;
              b2 = bf2 + bf3*NB;

              T1 = 0.5 * SmartConj(list[iMat].X[b1]);
              T2 = 0.5 * SmartConj(list[iMat].X[b2]);

            for(auto l = 0ul, bf4 = bf4_s; l < n4; l++, bf4++, ijkl++) { 

              // Indicies are swapped here to loop over contiguous memory
                
              // K(1,3) += 0.5 * I * X(2,4) = 0.5 * I * CONJ(X(4,2)) (**HER**)
              AX_loc[iMat][b1]           += 0.5 * SmartConj(list[iMat].X[bf4+NB*bf2]) * intBuffer_loc[ijkl];

              // K(4,2) += 0.5 * I * X(3,1) = 0.5 * I * CONJ(X(1,3)) (**HER**)
              AX_loc[iMat][bf4 + bf2*NB] += T1 * intBuffer_loc[ijkl];

              // K(4,1) += 0.5 * I * X(3,2) = 0.5 * I * CONJ(X(2,3)) (**HER**)
              AX_loc[iMat][bf4 + bf1*NB] += T2 * intBuffer_loc[ijkl];

              // K(2,3) += 0.5 * I * X(1,4) = 0.5 * I * CONJ(X(4,1)) (**HER**)
              AX_loc[iMat][b2]           += 0.5 * SmartConj(list[iMat].X[bf4+NB*bf1]) * intBuffer_loc[ijkl];

            } // l loop
            } // ijk

          // Nonhermetian contraction
          } else {

            if( list[iMat].contType == COULOMB )
            for(auto i = 0ul, bf1 = bf1_s, ijkl(0ul); i < n1; i++, bf1++)      
            for(auto j = 0ul, bf2 = bf2_s; j < n2; j++, bf2++) { 
              // Cache i,j variables
              b1 = bf1 + NB*bf2; 
              T1 = *(list[iMat].X  + b1);
              Tp1 = (AX_loc[iMat] + b1);

              b2 = bf2 + NB*bf1; 
              T2 = *(list[iMat].X  + b2);
              Tp2 = (AX_loc[iMat] + b2);
            for(auto k = 0ul, bf3 = bf3_s; k < n3; k++, bf3++) 
            for(auto l = 0ul, bf4 = bf4_s; l < n4; l++, bf4++, ijkl++) { 

              // J(1,2) += I * X(4,3)
              *Tp1 += 0.5*( list[iMat].X[bf4 + bf3*NB] + list[iMat].X[bf3 + bf4*NB]) * intBuffer_loc[ijkl];

              // J(3,4) += I * X(2,1)
              AX_loc[iMat][bf3 + bf4*NB] +=  0.5*(T2+T1) * intBuffer_loc[ijkl];

              // J(2,1) += I * X(3,4)
              *Tp2 += 0.5*( list[iMat].X[bf4 + bf3*NB] + list[iMat].X[bf3 + bf4*NB]) * intBuffer_loc[ijkl];

              // J(4,3) += I * X(1,2)
              AX_loc[iMat][bf4 + bf3*NB] +=  0.5*(T2+T1) * intBuffer_loc[ijkl];

            } // kl loop
            } // ij loop

            else if( list[iMat].contType == EXCHANGE )
            for(auto i = 0ul, bf1 = bf1_s, ijkl(0ul); i < n1; i++, bf1++)      
            for(auto j = 0ul, bf2 = bf2_s; j < n2; j++, bf2++)       
            for(auto k = 0ul, bf3 = bf3_s; k < n3; k++, bf3++) {

              // Cache i,j,k variables
              b1 = bf1 + bf3*NB;
              b2 = bf2 + bf3*NB;

              T1 = 0.5 * list[iMat].X[b1];
              T2 = 0.5 * list[iMat].X[b2];

              b3 = bf3 + bf1*NB;
              b4 = bf3 + bf2*NB;

              T3 = 0.5 * list[iMat].X[b3];
              T4 = 0.5 * list[iMat].X[b4];
            for(auto l = 0ul, bf4 = bf4_s; l < n4; l++, bf4++, ijkl++) { 

              // K(3,1) += 0.5 * I * X(4,2)
              AX_loc[iMat][b3]           += 0.5 * list[iMat].X[bf4+NB*bf2] * intBuffer_loc[ijkl];

              // K(4,2) += 0.5 * I * X(3,1)
              AX_loc[iMat][bf4 + bf2*NB] += T3 * intBuffer_loc[ijkl];
 
              // K(4,1) += 0.5 * I * X(3,2)
              AX_loc[iMat][bf4 + bf1*NB] += T4 * intBuffer_loc[ijkl];

              // K(3,2) += 0.5 * I * X(4,1)
              AX_loc[iMat][b4]           += 0.5 * list[iMat].X[bf4+NB*bf1] * intBuffer_loc[ijkl];

              // K(1,3) += 0.5 * I * X(2,4)
              AX_loc[iMat][b1]           += 0.5 * list[iMat].X[bf2+NB*bf4] * intBuffer_loc[ijkl];

              // K(2,4) += 0.5 * I * X(1,3)
              AX_loc[iMat][bf2 + bf4*NB] += T1 * intBuffer_loc[ijkl];
 
              // K(1,4) += 0.5 * I * X(2,3)
              AX_loc[iMat][bf1 + bf4*NB] += T2 * intBuffer_loc[ijkl];

              // K(2,3) += 0.5 * I * X(1,4)
              AX_loc[iMat][b2]           += 0.5 * list[iMat].X[bf1+NB*bf4] * intBuffer_loc[ijkl];

            } // l loop
            } // ijk

          } // Symmetry check

        } // iMat loop

#endif

#endif

      } // loop s4
      } // loop s3

#ifdef _SUB_TIMINGS
      auto botInner = std::chrono::high_resolution_clock::now();

      durInner += botInner - topInner;
#endif

#ifdef _BATCH_DIRECT
      assert(nthreads == 1);

#ifdef _SUB_TIMINGS
      auto topSymm = std::chrono::high_resolution_clock::now();
#endif

      // Reorder and expand integrals into square matricies
      for(auto s3 = 0ul, bf3_s = 0ul, ijkl = 0ul; s3 < NS; s3++, 
        bf3_s += n3) { 
        n3 = basisSet_.shells[s3].size();

      for(auto s4 = 0ul, bf4_s = 0ul; s4 <= s3; s4++, bf4_s += n4) { 
        n4 = basisSet_.shells[s4].size();

        for(auto i = 0ul, bf1 = bf1_s; i < n1; i++, bf1++)      
        for(auto j = 0ul, bf2 = bf2_s; j < n2; j++, bf2++)       
        for(auto k = 0ul, bf3 = bf3_s; k < n3; k++, bf3++) 
        for(auto l = 0ul, bf4 = bf4_s; l < n4; l++, bf4++, ijkl++) { 

          intBuffer2_loc[bf4 + bf3*NB + j*nSQ_ + i*nSQ_*n2] = intBuffer_loc[ijkl];
          intBuffer2_loc[bf3 + bf4*NB + j*nSQ_ + i*nSQ_*n2] = intBuffer_loc[ijkl];

        }

      }
      }

#ifdef _SUB_TIMINGS
      auto botSymm = std::chrono::high_resolution_clock::now();
      durSymm += botSymm - topSymm;
#endif
      
     
#ifdef _SUB_TIMINGS
      auto topCont = std::chrono::high_resolution_clock::now();
#endif



      // Perform batched contractions
      for(auto &C : list ) {

        // J Contraction
        if( C.contType == COULOMB ) {

          Gemm('T','N',n1*n2,1,nSQ_,T(1.),intBuffer2_loc,nSQ_,C.X,nSQ_,
            T(0.),reinterpret_cast<G*>(intBuffer_loc),n1*n2);

          // Populate the lower triangle of J contraction storage
          for(auto i = 0ul, bf1 = bf1_s; i < n1; i++, bf1++)      
          for(auto j = 0ul, bf2 = bf2_s; j < n2; j++, bf2++)
            C.AX[bf1 + bf2*NB] = intBuffer_loc[j + i*n2]; 

        // K Contraction
        } else if( C.contType == EXCHANGE ) {

          for(auto i = 0ul, bf1 = bf1_s; i < n1; i++, bf1++)      
          for(auto j = 0ul, bf2 = bf2_s; j < n2; j++, bf2++) {

/*
            // T(m,n) = I(m,k) * X(n,k)
            Gemm('N','T',NB,NB,NB,T(1.),intBuffer2 +j*nSQ_ + i*n2*nSQ_,NB,
              C.X,NB,T(0.),reinterpret_cast<G*>(intBuffer),NB);

            for(auto nu = 0; nu < NB; nu++) {
              C.AX[nu + bf1*NB] += intBuffer[nu + bf2*NB];
              if(s1 != s2) { 
                C.AX[nu + bf2*NB] += intBuffer[nu + bf1*NB];
              }
            }
*/

            Gemm('N','T',NB,1,NB,T(1.),intBuffer2_loc +j*nSQ_ + i*n2*nSQ_,NB,
              C.X + bf2,NB,T(0.),reinterpret_cast<G*>(intBuffer_loc),NB);
            for(auto nu = 0; nu < NB; nu++) 
              C.AX[nu + bf1*NB] += intBuffer_loc[nu];

            if( s1 != s2 ) {

              Gemm('N','T',NB,1,NB,T(1.),intBuffer2_loc +j*nSQ_ + i*n2*nSQ_,NB,
                C.X + bf1,NB,T(0.),reinterpret_cast<G*>(intBuffer_loc),NB);
              for(auto nu = 0; nu < NB; nu++) 
                C.AX[nu + bf2*NB] += intBuffer_loc[nu];

            }

          } // ij loop

        } // Exchange check
      } // Loop over contractions

#ifdef _SUB_TIMINGS
      auto botCont = std::chrono::high_resolution_clock::now();
      durCont += botCont - topCont;
#endif

#endif

    }; // s2
    }; // s1


    }; // OpenMP context

    auto botDirect = std::chrono::high_resolution_clock::now();

#ifdef _REPORT_INTEGRAL_TIMINGS
    size_t nIntSkip = std::accumulate(nSkip.begin(),nSkip.end(),0);
    std::cerr << "Screened " << nIntSkip << std::endl;

    std::chrono::duration<double> durDirect = botDirect - topDirect;
    std::cerr << "Direct Contraction took " << durDirect.count() << " s\n"; 

#ifdef _SUB_TIMINGS
    std::cerr << "  " << durInner.count() << " (" << durInner.count() / durDirect.count() * 100 
              << "%) Inner loop" << std::endl;
#ifndef _FULL_DIRECT
    std::cerr << "  " << durZero.count() << " (" << durZero.count() / durDirect.count() * 100 
              << "%) Zeroing buffer" << std::endl;
    std::cerr << "  " << durSymm.count() << " (" << durSymm.count() / durDirect.count() * 100 
              << "%) Symmetrization loop" << std::endl;
    std::cerr << "  " << durCont.count() << " (" << durCont.count() / durDirect.count() * 100 
              << "%) Contraction loop" << std::endl;
#endif
#endif
    std::cerr << std::endl;
#endif


#ifdef _FULL_DIRECT

    for( auto iMat = 0; iMat < NMat;  iMat++ ) 
    for( auto iTh  = 0; iTh < nthreads; iTh++) {
  
    //prettyPrintSmart(std::cerr,"AX " + std::to_string(iMat) + " " + std::to_string(iTh),
    //  AXthreads[iTh][iMat],NB,NB,NB);

      if( list[iMat].HER ) {

        MatAdd('N','C',NB,NB,G(0.5),AXthreads[iTh][iMat],NB,G(0.5),
          AXthreads[iTh][iMat],NB,reinterpret_cast<G*>(intBuffer),NB);

        if( nthreads != 1 )
          MatAdd('N','N',NB,NB,G(1.),reinterpret_cast<G*>(intBuffer),NB,
            G(1.), list[iMat].AX,NB,list[iMat].AX,NB);
        else
          SetMat('N',NB,NB,G(1.),reinterpret_cast<G*>(intBuffer),NB,
            list[iMat].AX,NB);

      } else {

        if( nthreads != 1 )
          MatAdd('N','N',NB,NB,G(0.5),AXthreads[iTh][iMat],NB,
            G(1.), list[iMat].AX,NB,list[iMat].AX,NB);
        else 
          Scale(NB*NB,G(0.5),list[iMat].AX,1);


      //std::transform(AXthreads[iTh][iMat], AXthreads[iTh][iMat] + NB*NB, 
      //  list[iMat].AX, []( G x ) -> G { return x / 4.; } );
      }

    };
    
#else

    for( auto &C : list ) {

      // Symmetrize J contraction
      if( C.contType == COULOMB ) 
        HerMat('L',NB,C.AX,NB);
  
      // Inplace transpose of K contraction
      if( C.contType == EXCHANGE ) 
        IMatCopy('C',NB,NB,G(1.),C.AX,NB,NB);

    } // Loop over contractions

#endif

#ifdef _SUB_TIMINGS
    auto topFree = std::chrono::high_resolution_clock::now();
#endif

    // Free scratch space
    memManager_.free(intBuffer);
#ifdef _SHZ_SCREEN
    memManager_.free(ShBlkNorms_raw);
#endif
    if(AXRaw != nullptr) memManager_.free(AXRaw);

#ifdef _SUB_TIMINGS
    auto botFree = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> durFree = botFree - topFree;

    std::cerr << "Free took " << durFree.count() << "s" << std::endl;
#endif


    // Turn threads for LA back on
    SetLAThreads(LAThreads);

  };

}; // namespace ChronusQ

#endif
