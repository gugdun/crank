/*
===========================================================================
Copyright (C) 2026 gugdun

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, see <https://www.gnu.org/licenses/>.
===========================================================================
*/

#ifndef PACKED_H
#define PACKED_H

#ifdef _MSC_VER
#define PACKED_STRUCT __pragma(pack(push, 1)) struct
#define END_PACKED __pragma(pack(pop))
#else
#define PACKED_STRUCT struct __attribute__((packed))
#define END_PACKED
#endif

#endif
