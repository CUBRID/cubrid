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
#include <ctype.h>
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWPorting.h"
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

    m_sql += " /* SQL : ";
    m_sql += m_query.getSqlName();
    m_sql += " */";
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
    /**
     * Example :
     *
     * SELECT * FROM A
     * WHERE
     *  COL_A = :COL_A
     *  AND COL_B = #COL_B
     *  AND COL_C = 'BLAH BLAH #COL_C'
     *  AND @SESSSION_VAR := @SESSSION_VAR + 1
     *  AND COL_D = 'BLAH BLAH :COL_D'
     */
    const char *p = m_query.c_str();
    const char *q = p;
    char cQuotationMark = 0;
    char cToken = 0;

    while (*p)
      {
        if (*p == '\'' || *p == '"')
          {
            if (cQuotationMark == 0)
              {
                cQuotationMark = *p;
              }
            else if (cQuotationMark == *p)
              {
                cQuotationMark = 0;
              }
          }
        else if (*p == '#')
          {
            /**
             * 3. ` AND COL_B = ` is added to normal query part.
             * 5. ` AND COL_C = 'BLAH BLAH` is added to normal query part.
             */
            addQueryPart(cToken, q, p);
            q = p;
            cToken = *p;
          }
        else if (cQuotationMark == 0 && *p == ':')
          {
            /**
             * 1. `SELECT * FROM A WHERE COL_A = ` is added to normal query part.
             * 7. `' AND @SESSSION_VAR ` is added to normal query part.
             */
            addQueryPart(cToken, q, p);
            q = p;
            cToken = *p;
          }
        else if (cToken != 0 && !isalnum(*p) && (*p != '_' && *p != '-'))
          {
            if (cToken == ':' && p - q > 1)
              {
                /**
                 * 2. `:COL_A` is added to bind query part.
                 */
                addQueryPart(cToken, q, p);
                q = p;
              }
            else if (cToken == '#')
              {
                /**
                 * 4. `#COL_B` is added to replace query part.
                 * 6. `#COL_C` is added to replace query part.
                 */
                addQueryPart(cToken, q, p);
                q = p;
              }
            cToken = 0;
          }
        ++p;
      }
    /**
     * 8. `:= @SESSSION_VAR + 1 AND COL_D = 'BLAH BLAH :COL_D'` is added to normal query part.
     */
    addQueryPart(cToken, q, p);
  }

  DBGWQuery::~DBGWQuery()
  {
  }

  void DBGWQuery::addQueryPart(char cToken, const char *szStart, const char *szEnd)
  {
    string part;
    DBGWQueryPartSharedPtr p;
    switch (cToken)
      {
      case '#':
        part = string(szStart + 1, szEnd - szStart - 1);
        p = DBGWQueryPartSharedPtr(new DBGWReplaceQueryPart(m_logger, part));
        m_queryPartList.push_back(p);
        break;
      case ':':
        part = string(szStart + 1, szEnd - szStart - 1);
        m_bindParamNameList.push_back(part);
        p = DBGWQueryPartSharedPtr(new DBGWSQLQueryPart("?"));
        m_queryPartList.push_back(p);
        break;
      default:
        part = string(szStart, szEnd - szStart);
        p = DBGWQueryPartSharedPtr(new DBGWSQLQueryPart(part));
        m_queryPartList.push_back(p);
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
