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
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWLogger.h"
#include "DBGWValue.h"
#include "DBGWQuery.h"

namespace dbgw
{

  /**
   * class DBGWBoundQuery
   */
  DBGWBoundQuery::DBGWBoundQuery(const char *szSql, const char *szGroupName,
      const DBGWQuery &query) :
    m_sql(szSql), m_groupName(szGroupName), m_query(query)
  {
    m_sqlKey += m_sql;
  }

  DBGWBoundQuery::DBGWBoundQuery(const DBGWBoundQuery &query) :
    m_sql(query. m_sql), m_groupName(query.m_groupName),
    m_sqlKey(query.m_sqlKey), m_query(query.m_query)
  {
  }

  DBGWBoundQuery::~DBGWBoundQuery()
  {
  }

  const char *DBGWBoundQuery::getSQL() const
  {
    return m_sql.c_str();
  }

  const char *DBGWBoundQuery::getSqlName() const
  {
    return m_query.getSqlName();
  }

  const char *DBGWBoundQuery::getGroupName() const
  {
    return m_groupName.c_str();
  }

  string DBGWBoundQuery::getSqlKey() const
  {
    return m_sqlKey;
  }

  DBGWQueryType::Enum DBGWBoundQuery::getType() const
  {
    return m_query.getType();
  }

  int DBGWBoundQuery::getBindNum() const
  {
    return m_query.getBindNum();
  }

  DBGWQueryParameter DBGWBoundQuery::getBindParam(int nIndex) const
  {
    return m_query.getBindParam(nIndex);
  }

  /**
   * class DBGWQuery
   */
  DBGWQuery::DBGWQuery(const string &fileName, const string &query,
      const string &sqlName, const string &groupName,
      DBGWQueryType::Enum queryType,
      const DBGWQueryParameterHashMap &inQueryParamMap,
      const DBGWQueryParameterHashMap &outQueryParamMap) :
    m_logger(groupName, sqlName), m_fileName(fileName), m_query(query),
    m_sqlName(sqlName), m_groupName(groupName), m_queryType(queryType),
    m_inQueryParamMap(inQueryParamMap),
    m_outQueryParamMap(outQueryParamMap)
  {
    int nStart = 0;
    int nEnd = 0;
    int nLen = m_query.size();
    char cToken = 0;
    while (nEnd < nLen)
      {
        switch (m_query[nEnd])
          {
          case '\'':
          case '"':
            if (cToken == m_query[nEnd])
              {
                cToken = 0;
              }
            else if (cToken != '#' && cToken != ':')
              {
                cToken = m_query[nEnd];
              }
            break;
          case '#':
          case ':':
            if (cToken == '\'' || cToken == '"')
              {
                break;
              }

            if (cToken == '#' || cToken == ':')
              {
                InvalidSqlException e(fileName.c_str(), sqlName.c_str());
                DBGW_LOG_ERROR(e.what());
                throw e;
              }

            if (addQueryPart(cToken, nStart, nEnd, false))
              {
                cToken = m_query[nEnd];
                nStart = nEnd;
              }
            break;
          case ')':
          case ' ':
          case ',':
          case '\t':
          case '\n':
            if (addQueryPart(cToken, nStart, nEnd, false))
              {
                cToken = 0;
                nStart = nEnd;
              }
            break;
          }
        ++nEnd;
      }
    if (nStart != nEnd)
      {
        addQueryPart(cToken, nStart, nEnd, true);
      }
  }

  DBGWQuery::~DBGWQuery()
  {
  }

  bool DBGWQuery::addQueryPart(char cToken, int nStart, int nEnd, bool bForce)
  {
    DBGWQueryPartSharedPtr p;
    switch (cToken)
      {
      case '#':
        p = DBGWQueryPartSharedPtr(
            new DBGWReplaceQueryPart(m_logger,
                m_query. substr(nStart + 1, nEnd - nStart - 1)));
        m_queryPartList.push_back(p);
        return true;
      case ':':
        /**
         * in mysql, ':=' is session variable operator.
         */
        if (m_query[nStart + 1] == '=')
          {
            p = DBGWQueryPartSharedPtr(
                new DBGWSQLQueryPart(m_query. substr(nStart, nEnd - nStart)));
            m_queryPartList.push_back(p);
          }
        else
          {
            m_bindParamNameList.push_back(
                m_query. substr(nStart + 1, nEnd - nStart - 1));
            p = DBGWQueryPartSharedPtr(new DBGWSQLQueryPart("?"));
            m_queryPartList.push_back(p);
          }
        return true;
      default:
        if (bForce || m_query[nEnd] == '#' || m_query[nEnd] == ':')
          {
            p
              = DBGWQueryPartSharedPtr(
                  new DBGWSQLQueryPart(
                      m_query. substr(nStart, nEnd - nStart)));
            m_queryPartList.push_back(p);
            return true;
          }
        return false;
      }
  }

  DBGWBoundQuerySharedPtr DBGWQuery::getDBGWBoundQuery(const char *szGroupName,
      const DBGWValueSet *pValueSet) const
  {
    stringstream stream;
    for (DBGWQueryPartList::const_iterator it = m_queryPartList.begin(); it
        != m_queryPartList.end(); it++)
      {
        stream << (*it)->toString(pValueSet);
      }
    DBGWBoundQuerySharedPtr pQuery(
        new DBGWBoundQuery(stream.str().c_str(), szGroupName, *this));
    return pQuery;
  }

  const char *DBGWQuery::getFileName() const
  {
    return m_fileName.c_str();
  }

  const char *DBGWQuery::getSqlName() const
  {
    return m_sqlName.c_str();
  }

  const char *DBGWQuery::getGroupName() const
  {
    return m_groupName.c_str();
  }

  DBGWQueryType::Enum DBGWQuery::getType() const
  {
    return m_queryType;
  }

  int DBGWQuery::getBindNum() const
  {
    return m_bindParamNameList.size();
  }

  DBGWQueryParameter DBGWQuery::getBindParam(size_t nIndex) const
  {
    if (m_bindParamNameList.size() < nIndex)
      {
        NotExistParamException e(nIndex);
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    DBGWQueryParameterHashMap::const_iterator it = m_inQueryParamMap.find(
        m_bindParamNameList[nIndex]);
    if (it == m_inQueryParamMap.end())
      {
        NotExistParamException e(m_bindParamNameList[nIndex]);
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }
    return it->second;
  }

  const char *DBGWQuery::getQuery() const
  {
    return m_query.c_str();
  }

  /**
   * class DBGWQuery::DBGWSQLQueryPart
   */
  DBGWQuery::DBGWSQLQueryPart::DBGWSQLQueryPart(string sql) :
    m_sql(sql)
  {
  }

  DBGWQuery::DBGWSQLQueryPart::~DBGWSQLQueryPart()
  {
  }

  string DBGWQuery::DBGWSQLQueryPart::toString(const DBGWValueSet *pValueSet) const
  {
    return m_sql;
  }

  /**
   * class DBGWQuery::DBGWReplaceQueryPart
   */
  DBGWQuery::DBGWReplaceQueryPart::DBGWReplaceQueryPart(const DBGWLogger &logger,
      const string &name) :
    m_logger(logger), m_name(name)
  {
  }

  DBGWQuery::DBGWReplaceQueryPart::~DBGWReplaceQueryPart()
  {
  }

  string DBGWQuery::DBGWReplaceQueryPart::toString(const DBGWValueSet *pValueSet) const
  {
    if (pValueSet == NULL)
      {
        NotExistParamException e(m_name.c_str());
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    const DBGWValue *pValue = pValueSet->getValue(m_name.c_str());
    if (pValue == NULL)
      {
        NotExistParamException e(m_name.c_str());
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }
    return pValue->toString();
  }

}
