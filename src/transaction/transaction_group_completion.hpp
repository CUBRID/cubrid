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

//
// Interface used to control group creation.
//

#ifndef _TRANSACTION_GROUP_COMPLETION_HPP_
#define _TRANSACTION_GROUP_COMPLETION_HPP_

#include "cubstream.hpp"

//
// group completion is the common interface used to control group creation.
//
class group_completion
{
public:
  virtual void complete_upto_stream_position (cubstream::stream_position stream_position) = 0;
  virtual void set_close_info_for_current_group (cubstream::stream_position stream_position,
    int count_expected_transactions) = 0;
};

#endif // !_TRANSACTION_GROUP_COMPLETION_HPP_
