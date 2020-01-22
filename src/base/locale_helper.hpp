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
 * locale_helper.hpp
 */

#ifndef _LOCALE_HELPER_HPP_
#define _LOCALE_HELPER_HPP_

#include "language_support.h"
#include "intl_support.h"

#include <regex>
#include <locale>
#include <codecvt>
#include <string>

namespace cublocale
{
    struct cub_regex_traits : std::regex_traits<char> {
        template< class Iter >
        string_type lookup_collatename( Iter first, Iter last ) const 
        {
        throw std::regex_error (std::regex_constants::error_collate);
        }
    };
}

#endif /* _LOCALE_HELPER_HPP_ */