/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * packing_common.hpp
 */

#ident "$Id$"

#ifndef _PACKING_COMMON_HPP_
#define _PACKING_COMMON_HPP_

#include <set>
#include <assert.h>
#include <cstddef>
#include "error_code.h"

#define NOT_IMPLEMENTED() \
  do \
    { \
      throw ("Not implemented"); \
    } \
  while (0)

typedef unsigned char BUFFER_UNIT;

class pinnable;

class pinner
{
public:
  int pin (pinnable *reference);
  int unpin (pinnable *reference);

  int unpin_all (void);

  ~pinner () { assert (references.size() == 0); }

private:
  std::set <pinnable*> references;
};

class pinnable
{
public:
  int add_pinner (pinner *referencer) { pinners.insert (referencer); return NO_ERROR; }
  int remove_pinner (pinner *referencer) { pinners.erase (referencer); return NO_ERROR; }
  int get_pin_count (void) { return (int) pinners.size(); }

  ~pinnable () { assert (pinners.size() == 0); }

private:
  std::set <pinner*> pinners;

};

#endif /* _PACKING_COMMON_HPP_ */
