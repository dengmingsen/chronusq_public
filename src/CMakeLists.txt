#
# This file is part of the Chronus Quantum (ChronusQ) software package
# 
# Copyright (C) 2014-2017 Li Research Group (University of Washington)
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
# 
# Contact the Developers:
#   E-Mail: xsli@uw.edu
#
add_subdirectory(cqlinalg)
add_subdirectory(grid)
add_subdirectory(molecule)
add_subdirectory(basisset)
add_subdirectory(aointegrals)
add_subdirectory(quantum)
add_subdirectory(wavefunction)
add_subdirectory(singleslater)
add_subdirectory(realtime)



list(REVERSE CQEX_LINK)
add_subdirectory(cxxapi)

set(CQEX_LINK ${CQEX_LINK} PARENT_SCOPE)
