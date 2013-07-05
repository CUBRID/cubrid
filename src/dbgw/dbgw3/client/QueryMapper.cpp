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

#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Value.h"
#include "dbgw3/SynchronizedResource.h"
#include "dbgw3/system/Mutex.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/client/ConfigurationObject.h"
#include "dbgw3/client/Resource.h"
#include "dbgw3/client/Configuration.h"
#include "dbgw3/client/QueryMapper.h"
#include "dbgw3/client/Connector.h"
#include "dbgw3/client/Query.h"

namespace dbgw
{

  static const char *GROUP_NAME_ALL = "__ALL__";
  static const char *GROUP_NAME_FIRST = "__FIRST__";

  _QueryMapper::_QueryMapper(Configuration *pConfiguration) :
    _ConfigurationObject(pConfiguration),
    m_version(DBGW_QUERY_MAP_VER_UNKNOWN)
  {
  }

  _QueryMapper::~_QueryMapper()
  {
  }

  void _QueryMapper::addQuery(const std::string &sqlName,
      trait<_Query>::sp pQuery)
  {
    _QuerySqlHashMap::iterator it = m_querySqlMap.find(sqlName);
    if (it != m_querySqlMap.end())
      {
        trait<_Query>::spvector &queryGroupList = it->second;
        if (!strcmp(pQuery->getGroupName(), GROUP_NAME_ALL)
            && !queryGroupList.empty())
          {
            DuplicateSqlNameException e(sqlName.c_str(), pQuery->getFileName(),
                queryGroupList[0]->getFileName());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        trait<_Query>::spvector::const_iterator it = queryGroupList.begin();
        for (; it != queryGroupList.end(); it++)
          {
            if (!strcmp(pQuery->getGroupName(), (*it)->getGroupName()))
              {
                DuplicateSqlNameException e(sqlName.c_str(),
                    pQuery->getFileName(), (*it)->getFileName());
                DBGW_LOG_ERROR(e.what());
                throw e;
              }
          }

        queryGroupList.push_back(pQuery);
      }
    else
      {
        trait<_Query>::spvector queryGroupList;
        queryGroupList.push_back(pQuery);
        m_querySqlMap[sqlName] = queryGroupList;
      }

    DBGW_LOGF_INFO("%s.%s in %s is normally loaded.", pQuery->getGroupName(),
        sqlName.c_str(), pQuery->getFileName());
  }

  void _QueryMapper::copyFrom(const _QueryMapper &src)
  {
    m_querySqlMap = _QuerySqlHashMap(src.m_querySqlMap.begin(),
        src.m_querySqlMap.end());
  }

  void _QueryMapper::setVersion(QueryMapperVersion version)
  {
    m_version = version;
  }

  void _QueryMapper::parseQuery(trait<_Connector>::sp pConnector)
  {
    _QuerySqlHashMap::iterator it = m_querySqlMap.begin();
    for (; it != m_querySqlMap.end(); it++)
      {
        trait<_Query>::spvector &queryGroupList = it->second;
        trait<_Query>::spvector::iterator git = queryGroupList.begin();
        for (; git != queryGroupList.end(); git++)
          {
            sql::DataBaseType dbType = pConnector->getDbType(
                (*git)->getGroupName());
            (*git)->setDbType(dbType);
            (*git)->parseQuery();
          }
      }
  }

  size_t _QueryMapper::size() const
  {
    return m_querySqlMap.size();
  }

  _QueryNameList _QueryMapper::getQueryNameList() const
  {
    _QueryNameList list;
    _QuerySqlHashMap::const_iterator it = m_querySqlMap.begin();
    while (it != m_querySqlMap.end())
      {
        list.push_back(it->first);
        ++it;
      }

    return list;
  }

  trait<_BoundQuery>::sp _QueryMapper::getQuery(const char *szSqlName,
      const char *szGroupName, const _Parameter &parameter,
      bool bFirstGroup) const
  {
    _QuerySqlHashMap::const_iterator it = m_querySqlMap.find(
        szSqlName);
    if (it == m_querySqlMap.end())
      {
        NotExistQueryInXmlException e(szSqlName);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    const trait<_Query>::spvector &queryGroupList = it->second;
    trait<_Query>::spvector::const_iterator qit = queryGroupList.begin();
    for (; qit != queryGroupList.end(); qit++)
      {
        if (bFirstGroup && !strcmp((*qit)->getGroupName(), GROUP_NAME_FIRST))
          {
            return (*qit)->getBoundQuery(szGroupName, parameter);
          }
        else if (!strcmp((*qit)->getGroupName(), GROUP_NAME_ALL) || !strcmp(
            (*qit)->getGroupName(), szGroupName))
          {
            return (*qit)->getBoundQuery(szGroupName, parameter);
          }
      }

    return trait<_BoundQuery>::sp();
  }

  QueryMapperVersion _QueryMapper::getVersion() const
  {
    return m_version;
  }

}
