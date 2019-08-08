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
 * generic_utils.hpp - Utilities for generic programming usage
 */

#ifndef _GENERIC_UTILS_HPP_
#define _GENERIC_UTILS_HPP_

#include <type_traits>

namespace cubbase
{
  template <template <typename> class G>
  struct conversion_tester
  {
    template <typename T>
    conversion_tester (const G<T> &);
  };

  template <typename From, template <typename> class To>
  struct is_instance_of
  {
    // Tests whether From is derived from a specialization of To or is itself a specialization
    static constexpr bool value = std::is_convertible<From,conversion_tester<To>>::value;
  };
}

#endif //_GENERIC_UTILS_HPP_