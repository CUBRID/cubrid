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
 * master_replication_channel.hpp
 */

#ident "$Id$"

#ifndef _MASTER_REPLICATION_CHANNEL_HPP_
#define _MASTER_REPLICATION_CHANNEL_HPP_

#include "packing_stream.hpp"
#include <vector>

namespace cubreplication
{

class log_file;

class master_replication_channel_manager
{
public:
  master_replication_channel_manager (const cubstream::stream_position &start_position);

  int init (const cubstream::stream_position &start_position);



  static master_replication_channel_manager *get_instance (void);


  cubstream::packing_stream * get_write_stream (void) { return generator_stream; };

private:
  /* file attached to log_generator (only for global instance) */
  log_file *m_file; 

  cubstream::packing_stream *generator_stream;
};

} /* namespace cubreplication */

#endif /* _MASTER_REPLICATION_CHANNEL_HPP_ */
