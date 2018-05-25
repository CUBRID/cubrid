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
 * pinning.cpp
 */

#ident "$Id$"

#include "pinning.hpp"

namespace cubbase
{
  int pinner::pin (pinnable *reference)
  {
    if (reference != NULL
	&& reference->add_pinner (this) == NO_ERROR)
      {
	references.insert (reference);
	return NO_ERROR;
      }

    return NO_ERROR;
  }

  int pinner::unpin (pinnable *reference)
  {
    if (reference->remove_pinner (this) == NO_ERROR)
      {
	references.erase (reference);
	return NO_ERROR;
      }

    return NO_ERROR;
  }

  int pinner::unpin_all (void)
  {
    for (auto it = references.begin (); it != references.end ();)
      {
	unpin (*it++);
      }

    return NO_ERROR;
  }

} /* namespace pinning */
