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
 * replication_apply_db_copy.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_APPLY_DB_COPY_HPP_
#define _REPLICATION_APPLY_DB_COPY_HPP_

namespace cubstream
{
  class multi_thread_stream;
  class stream_file;
  class transfer_receiver;
};

namespace cubreplication
{
  class row_object;

  class apply_copy_context
  {
    public:
      apply_copy_context ();

      ~apply_copy_context () {}

      init (void);

    private:
      cubstream::multi_thread_stream *m_stream;
      cubstream::stream_file *m_stream_file;
      cubstream::transfer_receiver *m_transfer_receiver;
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_APPLY_DB_COPY_HPP_ */
