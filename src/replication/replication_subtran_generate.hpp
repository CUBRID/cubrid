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
// generate replication for sub-transactions (changes of serial/click counter)
//

#ifndef _REPLICATION_SUBTRAN_GENERATE_HPP_
#define _REPLICATION_SUBTRAN_GENERATE_HPP_

#include "log_generator.hpp"

namespace cubreplication
{
  class subtran_generate
  {
    public:

      subtran_generate ();
      ~subtran_generate ();

      void start ();
      void commit ();
      void abort ();

      log_generator &get_repl_generator ();

    private:
      log_generator m_generator;
      bool m_started;
  };
} // namespace cubreplication

#endif // !_REPLICATION_SUBTRAN_GENERATE_HPP_
