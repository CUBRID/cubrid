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
 * type_traits_utils.hpp - Utilities for type traits information
 */

#ifndef _TYPE_TRAITS_UTILS_HPP_
#define _TYPE_TRAITS_UTILS_HPP_

#include <type_traits>

namespace cubbase
{
  template <template <typename> class G>
  struct conversion_tester
  {
    // Non-explicit constructors enable implicit conversion from the type of the constructor's argument to
    // constructor's class type. Here, concretely, we enable implicit conversions from G<T> to conversion_tester<G>
    // thus accepting from the point of view of is_instance_of<From, To<typnemae>> any From that is either of the form:
    // From : To<Something> or From = To<Something> (conditions necessary to have aforementioned conversions
    // avilable and implicitly std::is_convertible<From,conversion_tester<To>>::value true)
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

#endif // _TYPE_TRAITS_UTILS_HPP_
