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
#ifndef __INCLUDED_BASISSET_REFERENCE_HPP__
#define __INCLUDED_BASISSET_REFERENCE_HPP__

#include <chronusq_sys.hpp>
#include <molecule.hpp>

#include <libint2/shell.h>

namespace ChronusQ {

  /**
   *  \brief A struct to hold all of the information pertaining to the basis set
   *  of a particular atom
   *
   *  Also stores a copy of the unnormalized contraction coefficients in order to
   *  perform recontraction of uncontracted basis sets (as in Relativistic 
   *  calculations)
   */
  struct ReferenceShell {
  
    std::vector<libint2::Shell> shells;          ///< Atomic Shells
    std::vector<std::vector<double>> unNormCont; ///< Unnormalized coefficients
  
  }; // ReferenceShell struct
  
  /**
   *   \brief A class to handle the digestion of a complete basis set file
   *   to be used to generate BasisSet Objects.
   *
   *   Acts as a collection of ReferenceShell objects
   */
  class ReferenceBasisSet {
  
    std::string    basisPath_; ///< Path to basis file
    std::ifstream  basisFile_; ///< File object for basis file
  
    bool forceCart_;  ///< Whether or not to force cartesian basis functions
  
    // Functions to digest the basis set file
    // See src/basisset/reference.cxx for documentation
    void findBasisFile();
    void parseBasisFile();
  
  public:
  
    std::unordered_map<int,ReferenceShell> refShells; 
      ///< Full shell set for basis set
        
        
        
    // Disable default, copy and move constructors and assignment operators
    ReferenceBasisSet()                                     = delete;
    ReferenceBasisSet(const ReferenceBasisSet &)            = delete;
    ReferenceBasisSet(ReferenceBasisSet &&)                 = delete;
    ReferenceBasisSet& operator=(const ReferenceBasisSet &) = delete; 
    ReferenceBasisSet& operator=(ReferenceBasisSet &&)      = delete; 
  
    
    /**
     *  Path constructor.
     *
     *  Generates a ReferenceBasisSet object given a path to a basis
     *  file.
     *
     *  \param [in] path      Full path to basis file
     *  \param [in] forceCart Whether or not to force cartesian GTOs
     */ 
    ReferenceBasisSet(const std::string &path, bool forceCart = false) : 
      basisPath_(path), forceCart_(forceCart){
  
      findBasisFile();
      parseBasisFile();
    }
  
    
    // Generates a shell list given a Molecule object. 
    // See src/basisset/reference.cxx for documentation
    std::pair<std::vector<libint2::Shell>,std::vector<std::vector<double>>> 
      generateShellSet(const Molecule&);
  
  }; // ReferenceBasisSet class


}; // namespace ChronusQ
#endif