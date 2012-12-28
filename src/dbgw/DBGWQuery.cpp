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
#include <boost/lexical_cast.hpp>
#include "DBGWClient.h"
#include "DBGWWork.h"
#include "DBGWQuery.h"

namespace dbgw
{

  static const char *IMPLICIT_PARAM_NAME_FORMAT = "D%04d";
  static const int IMPLICIT_PARAM_NAME_SIZE = 6;

  string makeImplicitParamName(int nIndex)
  {
    char szImpliciParamName[IMPLICIT_PARAM_NAME_SIZE];
    snprintf(szImpliciParamName, IMPLICIT_PARAM_NAME_SIZE,
        IMPLICIT_PARAM_NAME_FORMAT, nIndex);
    return szImpliciParamName;
  }

  bool isImplicitParamName(const char *szName)
  {
    clearException();

    try
      {
        if (szName == NULL || strlen(szName) != IMPLICIT_PARAM_NAME_SIZE - 1)
          {
            return false;
          }

        if (szName[0] != 'D')
          {
            return false;
          }

        try
          {
            boost::lexical_cast<int>(szName + 1);
          }
        catch (boost::bad_lexical_cast &)
          {
            return false;
          }

        InvalidParamNameException e(szName);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return true;
      }
  }

  db::DBGWStatementType getQueryType(const char *szQueryType)
  {
    const char *p = szQueryType;

    while (isspace(*p))
      {
        ++p;
      }

    if (!strncasecmp(p, "select", 6))
      {
        return db::DBGW_STMT_TYPE_SELECT;
      }
    else if (!strncasecmp(p, "call", 4))
      {
        return db::DBGW_STMT_TYPE_PROCEDURE;
      }
    else
      {
        return db::DBGW_STMT_TYPE_UPDATE;
      }
  }

  /**
   * class DBGWBoundQuery
   */
  _DBGWBoundQuery::_DBGWBoundQuery(const char *szSql, const char *szGroupName,
      const _DBGWQuery &query) :
    m_sql(szSql), m_groupName(szGroupName), m_query(query)
  {
    m_sqlKey = m_sql;

    m_sql += " /* SQL : ";
    m_sql += m_query.getSqlName();
    m_sql += " */";
  }

  _DBGWBoundQuery::_DBGWBoundQuery(const _DBGWBoundQuery &query) :
    m_sql(query. m_sql), m_groupName(query.m_groupName),
    m_sqlKey(query.m_sqlKey), m_query(query.m_query)
  {
  }

  _DBGWBoundQuery::~_DBGWBoundQuery()
  {
  }

  const char *_DBGWBoundQuery::getSQL() const
  {
    return m_sql.c_str();
  }

  const char *_DBGWBoundQuery::getSqlName() const
  {
    return m_query.getSqlName();
  }

  const char *_DBGWBoundQuery::getGroupName() const
  {
    return m_groupName.c_str();
  }

  string _DBGWBoundQuery::getSqlKey() const
  {
    return m_sqlKey;
  }

  db::DBGWStatementType _DBGWBoundQuery::getType() const
  {
    return m_query.getType();
  }

  int _DBGWBoundQuery::getBindNum() const
  {
    return m_query.getBindNum();
  }

  const _DBGWQueryParameter &_DBGWBoundQuery::getQueryParam(size_t nIndex) const
  {
    return m_query.getQueryParam(nIndex);
  }

  const _DBGWQueryParameter &_DBGWBoundQuery::getQueryParamByPlaceHolderIndex(
      size_t nBindIndex) const
  {
    return m_query.getQueryParamByPlaceHolderIndex(nBindIndex);
  }

  const _DBGWQueryParameterList &_DBGWBoundQuery::getQueryParamList() const
  {
    return m_query.getQueryParamList();
  }

  const db::DBGWResultSetMetaDataSharedPtr _DBGWBoundQuery::getUserDefinedResultSetMetaData() const
  {
    return m_query.getUserDefinedResultSetMetaData();
  }

  bool _DBGWBoundQuery::isExistOutBindParam() const
  {
    return m_query.isExistOutBindParam();
  }

  _DBGWQuery::_DBGWQuery(DBGWQueryMapperVersion version, const string &fileName,
      const string &query, const string &sqlName, const string &groupName,
      db::DBGWStatementType statementType,
      const _DBGWQueryParameterList &queryParamList,
      db::DBGWResultSetMetaDataSharedPtr pUserDefinedResultSetMetaData) :
    m_logger(groupName, sqlName), m_fileName(fileName), m_query(query),
    m_sqlName(sqlName), m_groupName(groupName), m_statementType(statementType),
    m_queryParamList(queryParamList),
    m_pUserDefinedResultSetMetaData(pUserDefinedResultSetMetaData),
    m_bExistOutBindParam(false)
  {
    _DBGWQueryParameterList::const_iterator it = m_queryParamList.begin();
    for (; it != m_queryParamList.end(); it++)
      {
        if (it->mode == db::DBGW_PARAM_MODE_OUT
            || it->mode == db::DBGW_PARAM_MODE_INOUT)
          {
            m_bExistOutBindParam = true;
            break;
          }
      }

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
     *  AND ?
     *  AND COL_E = COL_D
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
        else if (cQuotationMark == 0 && (*p == ':' || *p == '?'))
          {
            /**
             * 1. `SELECT * FROM A WHERE COL_A = ` is added to normal query part.
             * 7. `' AND @SESSSION_VAR ` is added to normal query part.
             * 8. `:= @SESSSION_VAR + 1 AND COL_D = 'BLAH BLAH :COL_D' AND ` is added to normal query part.
             */
            addQueryPart(cToken, q, p);
            q = p;
            cToken = *p;
          }
        else if (cToken != 0 && !isalnum(*p) && (*p != '_' && *p != '-'))
          {
            if (cToken == '?' && version == DBGW_QUERY_MAP_VER_10)
              {
                /**
                 * 9. `?` is added to bind query part.
                 */
                addQueryPart(cToken, q, p);
                q = p;
              }
            else if (cToken == ':' && p - q > 1)
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
     * 10. `AND COL_E = COL_D` is added to normal query part.
     */
    addQueryPart(cToken, q, p);
  }

  _DBGWQuery::~_DBGWQuery()
  {
  }

  void _DBGWQuery::addQueryPart(char cToken, const char *szStart,
      const char *szEnd)
  {
    string part;
    _DBGWQueryPartSharedPtr p;

    if (cToken == '#')
      {
        part = string(szStart + 1, szEnd - szStart - 1);
        p = _DBGWQueryPartSharedPtr(new _DBGWReplaceQueryPart(m_logger, part));
        m_queryPartList.push_back(p);
      }
    else if (cToken == ':')
      {
        part = string(szStart + 1, szEnd - szStart - 1);

        int i = 0;
        _DBGWQueryParameterList::iterator it = m_queryParamList.begin();
        for (i = 0, it = m_queryParamList.begin();
            it != m_queryParamList.end(); i++, it++)
          {
            /**
             * find query parameter by placeholder name
             * and set each index to find easy later.
             */
            if (it->name == part)
              {
                _DBGWPlaceHolder placeHolder;
                placeHolder.name = part;
                placeHolder.index = m_placeHolderList.size();
                placeHolder.queryParamIndex = i;
                m_placeHolderList.push_back(placeHolder);

                if (it->firstPlaceHolderIndex == -1)
                  {
                    it->firstPlaceHolderIndex = placeHolder.index;
                  }

                p = _DBGWQueryPartSharedPtr(new _DBGWSQLQueryPart("?"));
                m_queryPartList.push_back(p);
                return;
              }
          }

        InvalidSqlException e(m_fileName.c_str(), part.c_str());
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }
    else if (cToken == '?')
      {
        _DBGWPlaceHolder placeHolder;
        placeHolder.index = m_placeHolderList.size();
        placeHolder.queryParamIndex = placeHolder.index;

        if (m_queryParamList.size() > placeHolder.index)
          {
            placeHolder.name = m_queryParamList[placeHolder.index].name;
            if (m_queryParamList[placeHolder.index].firstPlaceHolderIndex == -1)
              {
                m_queryParamList[placeHolder.index].firstPlaceHolderIndex =
                    placeHolder.index;
              }
          }

        if (placeHolder.name == "")
          {
            placeHolder.name = makeImplicitParamName(placeHolder.index);
          }

        m_placeHolderList.push_back(placeHolder);

        p = _DBGWQueryPartSharedPtr(new _DBGWSQLQueryPart("?"));
        m_queryPartList.push_back(p);
      }
    else
      {
        part = string(szStart, szEnd - szStart);
        p = _DBGWQueryPartSharedPtr(new _DBGWSQLQueryPart(part));
        m_queryPartList.push_back(p);
      }
  }

  _DBGWBoundQuerySharedPtr _DBGWQuery::getDBGWBoundQuery(const char *szGroupName,
      const _DBGWValueSet &valueSet) const
  {
    stringstream stream;
    for (_DBGWQueryPartList::const_iterator it = m_queryPartList.begin(); it
        != m_queryPartList.end(); it++)
      {
        stream << (*it)->toString(valueSet);
      }
    _DBGWBoundQuerySharedPtr pQuery(
        new _DBGWBoundQuery(stream.str().c_str(), szGroupName, *this));
    return pQuery;
  }

  const char *_DBGWQuery::getFileName() const
  {
    return m_fileName.c_str();
  }

  const char *_DBGWQuery::getSqlName() const
  {
    return m_sqlName.c_str();
  }

  const char *_DBGWQuery::getGroupName() const
  {
    return m_groupName.c_str();
  }

  db::DBGWStatementType _DBGWQuery::getType() const
  {
    return m_statementType;
  }

  int _DBGWQuery::getBindNum() const
  {
    return m_placeHolderList.size();
  }

  const _DBGWQueryParameter &_DBGWQuery::getQueryParam(size_t nIndex) const
  {
    if (m_queryParamList.size() < nIndex)
      {
        ArrayIndexOutOfBoundsException e(nIndex, "DBGWQueryParameter");
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    return m_queryParamList[nIndex];
  }

  const _DBGWQueryParameter &_DBGWQuery::getQueryParamByPlaceHolderIndex(
      size_t nBindIndex) const
  {
    if (m_placeHolderList.size() < nBindIndex)
      {
        ArrayIndexOutOfBoundsException e(nBindIndex, "DBGWQueryParameter");
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    const _DBGWPlaceHolder &placeHolder = m_placeHolderList[nBindIndex];
    if (m_queryParamList.size() < placeHolder.queryParamIndex)
      {
        ArrayIndexOutOfBoundsException e(placeHolder.queryParamIndex,
            "DBGWQueryParameter");
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    return m_queryParamList[placeHolder.queryParamIndex];
  }

  const char *_DBGWQuery::getQuery() const
  {
    return m_query.c_str();
  }

  const db::DBGWResultSetMetaDataSharedPtr _DBGWQuery::getUserDefinedResultSetMetaData() const
  {
    return m_pUserDefinedResultSetMetaData;
  }

  const _DBGWQueryParameterList &_DBGWQuery::getQueryParamList() const
  {
    return m_queryParamList;
  }

  bool _DBGWQuery::isExistOutBindParam() const
  {
    return m_bExistOutBindParam;
  }

  _DBGWQuery::_DBGWSQLQueryPart::_DBGWSQLQueryPart(string sql) :
    m_sql(sql)
  {
  }

  _DBGWQuery::_DBGWSQLQueryPart::~_DBGWSQLQueryPart()
  {
  }

  string _DBGWQuery::_DBGWSQLQueryPart::toString(
      const _DBGWValueSet &valueSet) const
  {
    return m_sql;
  }

  _DBGWQuery::_DBGWReplaceQueryPart::_DBGWReplaceQueryPart(
      const _DBGWLogger &logger,
      const string &name) :
    m_logger(logger), m_name(name)
  {
  }

  _DBGWQuery::_DBGWReplaceQueryPart::~_DBGWReplaceQueryPart()
  {
  }

  string _DBGWQuery::_DBGWReplaceQueryPart::toString(
      const _DBGWValueSet &valueSet) const
  {
    if (valueSet.size() == 0)
      {
        NotExistKeyException e(m_name.c_str(), "DBGWReplaceQueryPart");
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    const DBGWValue *pValue = valueSet.getValue(m_name.c_str());
    if (pValue == NULL)
      {
        NotExistKeyException e(m_name.c_str(), "DBGWReplaceQueryPart");
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }
    return pValue->toString();
  }

}
