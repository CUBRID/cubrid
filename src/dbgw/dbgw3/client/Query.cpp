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

#include <boost/lexical_cast.hpp>
#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Value.h"
#include "dbgw3/ValueSet.h"
#include "dbgw3/SynchronizedResource.h"
#include "dbgw3/system/DBGWPorting.h"
#include "dbgw3/system/ThreadEx.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/client/Configuration.h"
#include "dbgw3/client/Query.h"
#include "dbgw3/client/StatisticsMonitor.h"
#include "dbgw3/client/CharsetConverter.h"

namespace dbgw
{

  static const char *IMPLICIT_PARAM_NAME_FORMAT = "D%04d";
  static const int IMPLICIT_PARAM_NAME_SIZE = 6;

  std::string makeImplicitParamName(int nIndex)
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
    catch (Exception &e)
      {
        setLastException(e);
        return true;
      }
  }

  sql::StatementType getQueryType(const char *szQueryType)
  {
    const char *p = szQueryType;

    while (isspace(*p))
      {
        ++p;
      }

    if (!strncasecmp(p, "select", 6))
      {
        return sql::DBGW_STMT_TYPE_SELECT;
      }
    else if (!strncasecmp(p, "call", 4))
      {
        return sql::DBGW_STMT_TYPE_PROCEDURE;
      }
    else
      {
        return sql::DBGW_STMT_TYPE_UPDATE;
      }
  }

  /**
   * class BoundQuery
   */
  _BoundQuery::_BoundQuery(const char *szSql, const char *szGroupName,
      const _Query &query) :
    m_sql(szSql), m_groupName(szGroupName), m_query(query), m_pConverter(NULL)
  {
    m_sqlKey = m_sql;

    m_sql += " /* SQL : ";
    m_sql += m_query.getSqlName();
    m_sql += " */";
  }

  _BoundQuery::_BoundQuery(const _BoundQuery &query) :
    m_sql(query. m_sql), m_groupName(query.m_groupName),
    m_sqlKey(query.m_sqlKey), m_query(query.m_query), m_pConverter(NULL)
  {
  }

  _BoundQuery::~_BoundQuery()
  {
  }

  void _BoundQuery::bindCharsetConverter(_CharsetConverter *pConverter)
  {
    m_pConverter = pConverter;
  }

  std::string _BoundQuery::getSQL() const
  {
    if (m_pConverter != NULL)
      {
        return m_pConverter->convert(m_sql);
      }
    else
      {
        return m_sql;
      }
  }

  const char *_BoundQuery::getSqlName() const
  {
    return m_query.getSqlName();
  }

  const char *_BoundQuery::getGroupName() const
  {
    return m_groupName.c_str();
  }

  std::string _BoundQuery::getSqlKey() const
  {
    return m_sqlKey;
  }

  sql::StatementType _BoundQuery::getType() const
  {
    return m_query.getType();
  }

  int _BoundQuery::getBindNum() const
  {
    return m_query.getBindNum();
  }

  const _QueryParameter &_BoundQuery::getQueryParam(size_t nIndex) const
  {
    return m_query.getQueryParam(nIndex);
  }

  const _QueryParameter &_BoundQuery::getQueryParamByPlaceHolderIndex(
      size_t nBindIndex) const
  {
    return m_query.getQueryParamByPlaceHolderIndex(nBindIndex);
  }

  const trait<_QueryParameter>::vector &_BoundQuery::getQueryParamList() const
  {
    return m_query.getQueryParamList();
  }

  const trait<ClientResultSetMetaData>::sp
  _BoundQuery::getUserDefinedResultSetMetaData() const
  {
    return m_query.getUserDefinedResultSetMetaData();
  }

  bool _BoundQuery::isExistOutBindParam() const
  {
    return m_query.isExistOutBindParam();
  }

  _StatisticsItemColumn &_BoundQuery::getStatColumn(
      _QueryStatColumn column) const
  {
    return m_query.getStatColumn(column);
  }

  class _Query::Impl
  {
  private:
    class _QueryPart;
    typedef std::vector<_QueryPart *> _QueryPartList;

  public:
    Impl(_Query *pSelf, trait<_StatisticsMonitor>::sp pMonitor,
        QueryMapperVersion version, const std::string &fileName,
        const std::string &query, const std::string &sqlName,
        const std::string &groupName, sql::StatementType statementType,
        const trait<_QueryParameter>::vector &queryParamList,
        trait<ClientResultSetMetaData>::sp pUserDefinedResultSetMetaData) :
      m_pSelf(pSelf), m_logger(groupName, sqlName), m_fileName(fileName),
      m_query(query), m_sqlName(sqlName), m_groupName(groupName),
      m_statementType(statementType), m_dbType(sql::DBGW_DB_TYPE_CUBRID),
      m_version(version), m_queryParamList(queryParamList),
      m_pUserDefinedResultSetMetaData(pUserDefinedResultSetMetaData),
      m_bExistOutBindParam(false), m_statItem("QS")
    {
      trait<_QueryParameter>::vector::const_iterator it =
          m_queryParamList.begin();
      for (; it != m_queryParamList.end(); it++)
        {
          if (it->mode == sql::DBGW_PARAM_MODE_OUT
              || it->mode == sql::DBGW_PARAM_MODE_INOUT)
            {
              m_bExistOutBindParam = true;
              break;
            }
        }

      m_statItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_STATIC,
              DBGW_STAT_VAL_TYPE_STRING, " ", 1));
      m_statItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_STATIC,
              DBGW_STAT_VAL_TYPE_STRING, "GROUP-NAME", 20));
      m_statItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_STATIC,
              DBGW_STAT_VAL_TYPE_STRING, "SQL-NAME", 20));
      m_statItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_ADD,
              DBGW_STAT_VAL_TYPE_LONG, "TOTAL-CNT", 10));
      m_statItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_ADD,
              DBGW_STAT_VAL_TYPE_LONG, "SUCC-CNT", 10));
      m_statItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_ADD,
              DBGW_STAT_VAL_TYPE_LONG, "FAIL-CNT", 10));
      m_statItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_AVG,
              DBGW_STAT_VAL_TYPE_DOUBLE, "AVG", 8));
      m_statItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_MAX,
              DBGW_STAT_VAL_TYPE_DOUBLE, "MAX", 8));

      m_statItem[DBGW_QUERY_STAT_COL_PADDING] = "*";
      m_statItem[DBGW_QUERY_STAT_COL_GROUPNAME] = m_groupName.c_str();
      m_statItem[DBGW_QUERY_STAT_COL_SQLNAME] = m_sqlName.c_str();

      m_statItem[DBGW_QUERY_STAT_COL_TOTAL_CNT].setRightAlign();
      m_statItem[DBGW_QUERY_STAT_COL_SUCC_CNT].setRightAlign();
      m_statItem[DBGW_QUERY_STAT_COL_FAIL_CNT].setRightAlign();

      m_statItem[DBGW_QUERY_STAT_COL_AVG_TIME].setRightAlign();
      m_statItem[DBGW_QUERY_STAT_COL_AVG_TIME].setPrecision(2);

      m_statItem[DBGW_QUERY_STAT_COL_MAX_TIME].setRightAlign();
      m_statItem[DBGW_QUERY_STAT_COL_MAX_TIME].setPrecision(2);

      std::string statKey = m_groupName;
      statKey += ".";
      statKey += m_sqlName;

      pMonitor->getQueryStatGroup()->addItem(statKey, &m_statItem);
    }

    ~Impl()
    {
      _QueryPartList::iterator it = m_queryPartList.begin();
      for (; it != m_queryPartList.end(); it++)
        {
          if (*it != NULL)
            {
              delete *it;
            }
        }
    }

    void setDbType(sql::DataBaseType dbType)
    {
      m_dbType = dbType;
    }

    void parseQuery()
    {
      m_queryPartList.clear();
      m_placeHolderList.clear();

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
               * 8. `:= @SESSSION_VAR + 1 AND COL_D = 'BLAH BLAH :COL_D' AND
               * ` is added to normal query part.
               */
              addQueryPart(cToken, q, p);
              q = p;
              cToken = *p;
            }
          else if (cToken != 0 && !isalnum(*p) && (*p != '_' && *p != '-'))
            {
              if (cToken == '?' && m_version == DBGW_QUERY_MAP_VER_10)
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

    void addQueryPart(char cToken, const char *szStart,
        const char *szEnd)
    {
      std::string part;
      _QueryPart *p = NULL;

      if (cToken == '#')
        {
          part = std::string(szStart + 1, szEnd - szStart - 1);
          p = new _ReplaceQueryPart(m_logger, part);
          m_queryPartList.push_back(p);
        }
      else if (cToken == ':')
        {
          part = std::string(szStart + 1, szEnd - szStart - 1);

          int i = 0;
          trait<_QueryParameter>::vector::iterator it = m_queryParamList.begin();
          for (i = 0, it = m_queryParamList.begin();
              it != m_queryParamList.end(); i++, it++)
            {
              /**
               * find query parameter by placeholder name
               * and set each index to find easy later.
               */
              if (it->name == part)
                {
                  _PlaceHolder placeHolder;
                  placeHolder.name = part;
                  placeHolder.index = m_placeHolderList.size();
                  placeHolder.queryParamIndex = i;
                  m_placeHolderList.push_back(placeHolder);

                  if (it->firstPlaceHolderIndex == -1)
                    {
                      it->firstPlaceHolderIndex = placeHolder.index;
                    }

                  if (m_dbType == sql::DBGW_DB_TYPE_ORACLE)
                    {
                      /**
                       * oci using placeholder name instead of '?'
                       */
                      p = new _SQLQueryPart(
                          ":" + makeImplicitParamName(m_queryPartList.size()));
                    }
                  else
                    {
                      p = new _SQLQueryPart("?");
                    }

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
          _PlaceHolder placeHolder;
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

          p = new _SQLQueryPart("?");
          m_queryPartList.push_back(p);
        }
      else
        {
          part = std::string(szStart, szEnd - szStart);
          p = new _SQLQueryPart(part);
          m_queryPartList.push_back(p);
        }
    }

    trait<_BoundQuery>::sp getBoundQuery(const char *szGroupName,
        const _ValueSet &valueSet) const
    {
      std::stringstream stream;
      for (_QueryPartList::const_iterator it = m_queryPartList.begin(); it
          != m_queryPartList.end(); it++)
        {
          stream << (*it)->toString(valueSet);
        }
      trait<_BoundQuery>::sp pQuery(
          new _BoundQuery(stream.str().c_str(), szGroupName, *m_pSelf));
      return pQuery;
    }

    const char *getFileName() const
    {
      return m_fileName.c_str();
    }

    const char *getSqlName() const
    {
      return m_sqlName.c_str();
    }

    const char *getGroupName() const
    {
      return m_groupName.c_str();
    }

    sql::StatementType getType() const
    {
      return m_statementType;
    }

    int getBindNum() const
    {
      return m_placeHolderList.size();
    }

    const _QueryParameter &getQueryParam(size_t nIndex) const
    {
      if (m_queryParamList.size() < nIndex)
        {
          ArrayIndexOutOfBoundsException e(nIndex, "DBGWQueryParameter");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }

      return m_queryParamList[nIndex];
    }

    const _QueryParameter &getQueryParamByPlaceHolderIndex(
        size_t nBindIndex) const
    {
      if (m_placeHolderList.size() < nBindIndex)
        {
          ArrayIndexOutOfBoundsException e(nBindIndex, "DBGWQueryParameter");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }

      const _PlaceHolder &placeHolder = m_placeHolderList[nBindIndex];
      if (m_queryParamList.size() < placeHolder.queryParamIndex)
        {
          ArrayIndexOutOfBoundsException e(placeHolder.queryParamIndex,
              "DBGWQueryParameter");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }

      return m_queryParamList[placeHolder.queryParamIndex];
    }

    const char *getQuery() const
    {
      return m_query.c_str();
    }

    const trait<ClientResultSetMetaData>::sp \
    getUserDefinedResultSetMetaData() const
    {
      return m_pUserDefinedResultSetMetaData;
    }

    const trait<_QueryParameter>::vector &getQueryParamList() const
    {
      return m_queryParamList;
    }

    bool isExistOutBindParam() const
    {
      return m_bExistOutBindParam;
    }

    _StatisticsItemColumn &getStatColumn(
        _QueryStatColumn column) const
    {
      return m_statItem[column];
    }

  private:
    class _QueryPart
    {
    public:
      virtual ~ _QueryPart() {}

      virtual std::string toString(const _ValueSet &valueSet) const = 0;
    };

    class _SQLQueryPart: public _QueryPart
    {
    public:
      _SQLQueryPart(std::string sql) :
        m_sql(sql)
      {
      }

      virtual ~ _SQLQueryPart() {}

      virtual std::string toString(const _ValueSet &valueSet) const
      {
        return m_sql;
      }

    private:
      std::string m_sql;
    };

    class _ReplaceQueryPart: public _QueryPart
    {
    public:
      _ReplaceQueryPart(const _Logger &logger, const std::string &name) :
        m_logger(logger), m_name(name)
      {
      }

      virtual ~ _ReplaceQueryPart() {}

      virtual std::string toString(const _ValueSet &valueSet) const
      {
        if (valueSet.size() == 0)
          {
            NotExistKeyException e(m_name.c_str(), "DBGWReplaceQueryPart");
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        const Value *pValue = valueSet.getValue(m_name.c_str());
        if (pValue == NULL)
          {
            NotExistKeyException e(m_name.c_str(), "DBGWReplaceQueryPart");
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
        return pValue->toString();
      }

    private:
      const _Logger &m_logger;
      std::string m_name;
    };

  private:
    _Query *m_pSelf;
    _Logger m_logger;
    _QueryPartList m_queryPartList;
    std::string m_fileName;
    std::string m_query;
    std::string m_sqlName;
    std::string m_groupName;
    sql::StatementType m_statementType;
    sql::DataBaseType m_dbType;
    QueryMapperVersion m_version;
    trait<_QueryParameter>::vector m_queryParamList;
    trait<_PlaceHolder>::vector m_placeHolderList;
    /**
     * in dbgw 1.0,
     * user can specify result set meta data.
     * so we have to save this to make result set.
     */
    trait<ClientResultSetMetaData>::sp m_pUserDefinedResultSetMetaData;
    bool m_bExistOutBindParam;

    _StatisticsItem m_statItem;
  };

  _Query::_Query(trait<_StatisticsMonitor>::sp pMonitor,
      QueryMapperVersion version,
      const std::string &fileName, const std::string &query,
      const std::string &sqlName, const std::string &groupName,
      sql::StatementType statementType,
      const trait<_QueryParameter>::vector &queryParamList,
      trait<ClientResultSetMetaData>::sp pUserDefinedResultSetMetaData) :
    m_pImpl(new Impl(this, pMonitor, version, fileName, query, sqlName,
        groupName, statementType, queryParamList, pUserDefinedResultSetMetaData))
  {
  }

  _Query::~_Query()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void _Query::setDbType(sql::DataBaseType dbType)
  {
    m_pImpl->setDbType(dbType);
  }

  void _Query::parseQuery()
  {
    m_pImpl->parseQuery();
  }

  trait<_BoundQuery>::sp _Query::getBoundQuery(const char *szGroupName,
      const _Parameter &valueSet) const
  {
    return m_pImpl->getBoundQuery(szGroupName, valueSet);
  }

  const char *_Query::getFileName() const
  {
    return m_pImpl->getFileName();
  }

  const char *_Query::getSqlName() const
  {
    return m_pImpl->getSqlName();
  }

  const char *_Query::getGroupName() const
  {
    return m_pImpl->getGroupName();
  }

  sql::StatementType _Query::getType() const
  {
    return m_pImpl->getType();
  }

  int _Query::getBindNum() const
  {
    return m_pImpl->getBindNum();
  }

  const _QueryParameter &_Query::getQueryParam(size_t nIndex) const
  {
    return m_pImpl->getQueryParam(nIndex);
  }

  const _QueryParameter &_Query::getQueryParamByPlaceHolderIndex(
      size_t nBindIndex) const
  {
    return m_pImpl->getQueryParamByPlaceHolderIndex(nBindIndex);
  }

  const char *_Query::getQuery() const
  {
    return m_pImpl->getQuery();
  }

  const trait<ClientResultSetMetaData>::sp _Query::getUserDefinedResultSetMetaData() const
  {
    return m_pImpl->getUserDefinedResultSetMetaData();
  }

  const trait<_QueryParameter>::vector &_Query::getQueryParamList() const
  {
    return m_pImpl->getQueryParamList();
  }

  bool _Query::isExistOutBindParam() const
  {
    return m_pImpl->isExistOutBindParam();
  }

  _StatisticsItemColumn &_Query::getStatColumn(
      _QueryStatColumn column) const
  {
    return m_pImpl->getStatColumn(column);
  }

}
