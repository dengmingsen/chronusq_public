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
#ifndef __INCLUDED_QUANTUM_PREPROCESSOR_HPP__
#define __INCLUDED_QUANTUM_PREPROCESSOR_HPP__

#define SPIN_OPERATOR_ALLOC(NB,X) \
  /* Always allocate Scalar matricies */ \
  X.emplace_back(this->memManager.template malloc<T>(NB*NB)); \
  \
  /* If 2C or open shell, populate Mz storage */ \
  if(this->nC > 1 or not this->iCS) \
    X.emplace_back(this->memManager.template malloc<T>(NB*NB)); \
  \
  /* If 2C, populate My / Mx */ \
  if(this->nC > 1) { \
    X.emplace_back(this->memManager.template malloc<T>(NB*NB)); \
    X.emplace_back(this->memManager.template malloc<T>(NB*NB)); \
  }


#endif
