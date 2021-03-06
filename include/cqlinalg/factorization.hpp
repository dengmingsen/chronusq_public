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
#ifndef __INCLUDED_CQLINALG_FACTORIZATION_HPP__
#define __INCLUDED_CQLINALG_FACTORIZATION_HPP__

#include <cqlinalg/cqlinalg_config.hpp>

namespace ChronusQ {

  /**
   *  \brief Computes the Cholesky factorization of an hermetian 
   *  positive definate matrix A. Smart wrapper around DPOTRF or 
   *  ZPOTRF depending on context.
   *
   *  See http://www.netlib.org/lapack/lapack-3.1.1/html/dpotrf.f.html or
   *      http://www.netlib.org/lapack/lapack-3.1.1/html/zpotrf.f.html for
   *  parameter documentation.
   */  
  template <typename _F>
  int Cholesky(char UPLO, int N, _F *A, int LDA);

  /**
   *  \brief Computes the inverse of an hermetian positive definate matrix A
   *  given its Cholesky decomposition. Smart wrapper around DPOTRI or 
   *  ZPOTRI depending on context.
   *
   *  Assumes A is the Cholesky factor upon entry
   *
   *  See http://www.netlib.org/lapack/lapack-3.1.1/html/dpotrf.f.html or
   *      http://www.netlib.org/lapack/lapack-3.1.1/html/zpotrf.f.html for
   *  parameter documentation.
   */  
  template <typename _F>
  int CholeskyInv(char UPLO, int N, _F *A, int LDA);


  /**
   *  \brief Computes the LU factorization of a matrix A. Smart wrapper
   *  around DGETRF or ZGETRF depending on context.
   *
   *  See http://www.netlib.org/lapack/lapack-3.1.1/html/dgetrf.f.html or
   *      http://www.netlib.org/lapack/lapack-3.1.1/html/zgetrf.f.html for
   *  parameter documentation.
   */ 
  template <typename _F>
  int LU(int M, int N, _F *A, int LDA, int *IPIV);


  /**
   *  \brief Computes the LU factorization of a matrix A. Wraps LU function
   *  with internal allocation of IPIV through a CQMemManager.
   */ 
  template <typename _F>
  int LU(int M, int N, _F *A, int LDA, CQMemManager &mem) {
    int *IPIV = mem.template malloc<int>(std::min(M,N));
    int INFO = LU(M,N,A,LDA,IPIV);
    mem.free(IPIV);
    return INFO;
  };

  /**
   *  \brief Computes the inverse of a non-singular matrix A. Initially
   *  computes the LU factorization then wraps DGETRI / ZGETRI for the
   *  matrix inversion depending on context.
   *
   *  See LU for docs on LU factorization. Allocates memory internally
   *  through CQMemManager
   *
   *  See http://www.netlib.org/lapack/lapack-3.1.1/html/dgetri.f.html or
   *      http://www.netlib.org/lapack/lapack-3.1.1/html/zgetri.f.html for
   *  parameter documentation.
   */ 
  template <typename _F>
  int LUInv(int N, _F *A, int LDA, CQMemManager &mem);


}; // namespace ChronusQ

#endif
