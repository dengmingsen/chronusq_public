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
#include <cqlinalg/blasext.hpp>

#include <util/matout.hpp>

namespace ChronusQ {

  // MatAdd generic template
  template <typename _F1, typename _F2, typename _F3, typename _FScale1, 
    typename _FScale2>
  void MatAdd(char TRANSA, char TRANSB, size_t M, size_t N, _FScale1 ALPHA, 
    _F1 *A, size_t LDA, _FScale2 BETA, _F2 *B, size_t LDB, _F3 *C, size_t LDC){

    if( TRANSA == 'N' and TRANSB == 'N' ) {
      #pragma omp parallel
      {
        _F1 *locA = A;
        _F2 *locB = B;
        _F3 *locC = C;
        #pragma omp for
        for(int j = 0; j < N; j++) {
          locA = A + j*LDA;
          locB = B + j*LDB;
          locC = C + j*LDC;
          #pragma omp simd
          for(int i = 0; i < M; i++)
            locC[i] = ALPHA * locA[i] + BETA * locB[i];
        }
      }
    } else {

      assert( M == N );
      if( TRANSA != 'N' ) 
        assert( reinterpret_cast<void*>(A) != reinterpret_cast<void*>(C) );
      if( TRANSB != 'N' )
        assert( reinterpret_cast<void*>(B) != reinterpret_cast<void*>(C) );

      Eigen::Map< Eigen::Matrix<_F1,
          Eigen::Dynamic,Eigen::Dynamic,Eigen::ColMajor> > AMap(A,LDA,N);
      Eigen::Map< Eigen::Matrix<_F2,
          Eigen::Dynamic,Eigen::Dynamic,Eigen::ColMajor> > BMap(B,LDB,N);
      Eigen::Map< Eigen::Matrix<_F3,
          Eigen::Dynamic,Eigen::Dynamic,Eigen::ColMajor> > CMap(C,LDC,N);

      if( TRANSA == 'C' and TRANSB == 'N' )
        CMap.block(0,0,M,N).noalias() = 
          ALPHA * AMap.block(0,0,M,N).adjoint().template cast<_F3>() +
          BETA  * BMap.block(0,0,M,N).template cast<_F3>();
      else if( TRANSA == 'N' and TRANSB == 'C' )
        CMap.block(0,0,M,N).noalias() = 
          ALPHA * AMap.block(0,0,M,N).template cast<_F3>() +
          BETA  * BMap.block(0,0,M,N).adjoint().template cast<_F3>();
      else if( TRANSA == 'C' and TRANSB == 'C' )
        CMap.block(0,0,M,N).noalias() = 
          ALPHA * AMap.block(0,0,M,N).adjoint().template cast<_F3>() +
          BETA  * BMap.block(0,0,M,N).adjoint().template cast<_F3>();
      else if( TRANSA == 'R' and TRANSB == 'N' )
        CMap.block(0,0,M,N).noalias() = 
          ALPHA * AMap.block(0,0,M,N).conjugate().template cast<_F3>() +
          BETA  * BMap.block(0,0,M,N).template cast<_F3>();
      else if( TRANSA == 'N' and TRANSB == 'R' )
        CMap.block(0,0,M,N).noalias() = 
          ALPHA * AMap.block(0,0,M,N).template cast<_F3>() +
          BETA  * BMap.block(0,0,M,N).conjugate().template cast<_F3>();
      else if( TRANSA == 'R' and TRANSB == 'R' )
        CMap.block(0,0,M,N).noalias() = 
          ALPHA * AMap.block(0,0,M,N).conjugate().template cast<_F3>() +
          BETA  * BMap.block(0,0,M,N).conjugate().template cast<_F3>();

    }

  }; // MatAdd generic template

  template void MatAdd( char, char, size_t, size_t, dcomplex, dcomplex*, 
    size_t, dcomplex, double*, size_t, dcomplex*, size_t);

#ifdef _CQ_MKL

  template<>
  void MatAdd(char TRANSA, char TRANSB, size_t M, size_t N, double ALPHA, 
    double *A, size_t LDA, double BETA, double *B, size_t LDB, double *C, 
    size_t LDC) {

      mkl_domatadd('C',TRANSA,TRANSB,M,N,ALPHA,A,LDA,BETA,B,LDB,C,LDC);

  }; // MatAdd (real, real, real)

  template<>
  void MatAdd(char TRANSA, char TRANSB, size_t M, size_t N, dcomplex ALPHA, 
    dcomplex *A, size_t LDA, dcomplex BETA, dcomplex *B, size_t LDB, 
    dcomplex *C, size_t LDC) {

      mkl_zomatadd('C',TRANSA,TRANSB,M,N,ALPHA,A,LDA,BETA,B,LDB,C,LDC);

  }; // MatAdd (complex, complex, complex)

#else

  template void MatAdd( char, char, size_t, size_t, dcomplex, dcomplex*, 
    size_t, dcomplex, dcomplex*, size_t, dcomplex*, size_t);

  template void MatAdd( char, char, size_t, size_t, double, double*, 
    size_t, double, double*, size_t, double*, size_t);

#endif


  // Generic IMatCopy template
  template <typename _F, typename _FScale>
  void IMatCopy(char TRANS, size_t M, size_t N, _FScale ALPHA, _F *A, 
    size_t LDA, size_t LDB) {


      assert( LDA == LDB );

      if( TRANS == 'N' and ALPHA == _FScale(1.) ) return;

      Eigen::Map< Eigen::Matrix<_F,
          Eigen::Dynamic,Eigen::Dynamic,Eigen::ColMajor> > AMap(A,LDA,N);

      if( TRANS == 'T' )      AMap.block(0,0,M,N).transposeInPlace();
      else if( TRANS == 'C' ) AMap.block(0,0,M,N).adjointInPlace();
      else if( TRANS == 'R' ) 
        AMap.block(0,0,M,N).noalias() = AMap.block(0,0,M,N).conjugate();

      if( ALPHA != _FScale(1.) ) AMap.block(0,0,M,N) *= ALPHA;

  }; // Generic IMatCopy template


#ifdef _CQ_MKL

  template <>
  void IMatCopy(char TRANS, size_t M, size_t N, double ALPHA, double *A, 
    size_t LDA, size_t LDB) {

    mkl_dimatcopy('C',TRANS,M,N,ALPHA,A,LDA,LDB);

  }

  template <>
  void IMatCopy(char TRANS, size_t M, size_t N, dcomplex ALPHA, dcomplex *A, 
    size_t LDA, size_t LDB) {

    mkl_zimatcopy('C',TRANS,M,N,ALPHA,A,LDA,LDB);

  }

#else

  template void IMatCopy(char,size_t,size_t,double,double*,size_t,size_t);
  template void IMatCopy(char,size_t,size_t,dcomplex,dcomplex*,size_t,size_t);

#endif
  
  template void IMatCopy(char,size_t,size_t,double,dcomplex*,size_t,size_t);


  // Generic HerMat template
  template <typename _F>
  void HerMat(char UPLO, size_t N, _F *A, size_t LDA) {

    assert( UPLO == 'L' or UPLO == 'U' );

    Eigen::Map< Eigen::Matrix<_F,
        Eigen::Dynamic,Eigen::Dynamic,Eigen::ColMajor> > AMap(A,LDA,N);

    if( UPLO == 'L' )  
      AMap.block(0,0,N,N)= 
        AMap.block(0,0,N,N).template selfadjointView<Eigen::Lower>();
    else
      AMap.block(0,0,N,N) = 
        AMap.block(0,0,N,N).template selfadjointView<Eigen::Upper>();


    if(std::is_same<_F,dcomplex>::value)
    for(auto i = 0; i < N; i++)
      A[i + i*LDA] = std::real(A[i + i*LDA]);

  }; // Generic HerMat template

  template void HerMat(char, size_t, double*, size_t);
  template void HerMat(char, size_t, dcomplex*, size_t);

}; // namespace ChronusQ
