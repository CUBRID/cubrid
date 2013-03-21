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

/**
 * This header file is deprecated.
 * You can use <client/Client.h> instead of this.
 */

#ifndef DBGWCLIENT_H_
#define DBGWCLIENT_H_

#include <dbgw3/client/Client.h>

namespace dbgw
{

  class Exception;
  typedef Exception DBGWException;

  class Value;
  typedef Value DBGWValue;

  class Configuration;
  typedef Configuration DBGWConfiguration;

  typedef _ConfigurationVersion _DBGWConfigurationVersion;

  class _QueryMapper;
  typedef _QueryMapper _DBGWQueryMapper;

  class Client;
  typedef Client DBGWClient;

  typedef _Parameter DBGWClientParameter;
  typedef trait<_Parameter>::vector DBGWClientParameterList;

  class ClientResultSet;
  typedef trait<ClientResultSet>::sp DBGWClientResultSetSharedPtr;
  typedef trait<ClientResultSet>::spvector DBGWClientResultSetSharedPtrList;

  class ClientResultSetMetaData;
  typedef trait<ClientResultSetMetaData>::sp DBGWResultSetMetaDataSharedPtr;

  class _MockManager;
  typedef _MockManager _DBGWMockManager;

  typedef ValueType DBGWValueType;

  typedef LobSharedPtr DBGWLobSharedPtr;

}

#endif
