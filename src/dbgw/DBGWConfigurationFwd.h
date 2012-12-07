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

#ifndef DBGWCONFIGURATIONFWD_H_
#define DBGWCONFIGURATIONFWD_H_

namespace dbgw
{

  class _DBGWHost;
  typedef boost::shared_ptr<_DBGWHost> _DBGWHostSharedPtr;
  typedef vector<_DBGWHostSharedPtr> _DBGWHostList;

  class _DBGWExecutorProxy;
  typedef boost::shared_ptr<_DBGWExecutorProxy> _DBGWExecutorProxySharedPtr;
  typedef boost::unordered_map<string, _DBGWExecutorProxySharedPtr,
          boost::hash<string>, dbgwStringCompareFunc> _DBGWExecutorProxyHashMap;

  class _DBGWExecutor;
  typedef boost::shared_ptr<_DBGWExecutor> _DBGWExecutorSharedPtr;
  typedef list<_DBGWExecutorSharedPtr> _DBGWExecutorList;

  class _DBGWExecutorPool;

  class _DBGWGroup;
  typedef boost::shared_ptr<_DBGWGroup> _DBGWGroupSharedPtr;
  typedef vector<_DBGWGroupSharedPtr> _DBGWGroupList;

  class _DBGWService;
  typedef boost::shared_ptr<_DBGWService> _DBGWServiceSharedPtr;
  typedef vector<_DBGWServiceSharedPtr> _DBGWServiceList;

  class _DBGWQueryMapper;
  typedef boost::shared_ptr<_DBGWQueryMapper> _DBGWQueryMapperSharedPtr;

  class DBGWConfiguration;
}

#endif
