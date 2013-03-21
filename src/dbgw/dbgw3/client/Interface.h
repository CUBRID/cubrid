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

#ifndef INTERFACE_H_
#define INTERFACE_H_

namespace dbgw
{

  class ClientResultSet;
  typedef trait<ClientResultSet>::sp ClientResultSetSharedPtr;
  typedef trait<ClientResultSet>::spvector ClientResultSetSharedPtrList;

  class _Parameter;
  typedef _Parameter ClientParameter;
  typedef std::vector<_Parameter> ClientParameterList;

  class ClientResultSetMetaData;
  typedef trait<ClientResultSetMetaData>::sp ClientResultSetMetaDataSharedPtr;

  class Lob;
  typedef trait<Lob>::sp LobSharedPtr;

}

#endif
