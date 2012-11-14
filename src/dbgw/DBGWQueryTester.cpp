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
#if defined(WINDOWS)
#include <expat/expat.h>
#else /* WINDOWS */
#include <expat.h>
#endif /* !WINDOWS */
#include "DBGWClient.h"
#include "DBGWXMLParser.h"
#include "DBGWQueryTester.h"

namespace dbgw
{

  static const char *XML_NODE_SCENARIO = "scenario";
  static const char *XML_NODE_SCENARIO_PROP_NAMESPAE = "namespace";

  static const char *XML_NODE_TRANSACTION = "transaction";

  static const char *XML_NODE_EXECUTE = "execute";
  static const char *XML_NODE_EXECUTE_PROP_SQL_NAME = "sql-name";
  static const char *XML_NODE_EXECUTE_PROP_DUMMY = "dummy";

  static const char *XML_NODE_PARAM = "param";
  static const char *XML_NODE_PARAM_PROP_NAME = "name";
  static const char *XML_NODE_PARAM_PROP_TYPE = "type";
  static const char *XML_NODE_PARAM_PROP_VALUE = "value";
  static const char *XML_NODE_PARAM_PROP_ISNULL = "is-null";

  DBGWQueryTester::DBGWQueryTester(const char *szSqlName, bool bDummy) :
    m_sqlName(szSqlName), m_bDummy(bDummy)
  {
  }

  DBGWQueryTester::~DBGWQueryTester()
  {
  }

  void DBGWQueryTester::addParameter(const char *szName, DBGWValueType type,
      const char *szValue, bool bNull)
  {
    int nValue;
    int64 lValue;
    void *pValue = (void *) szValue;

    if (type == DBGW_VAL_TYPE_INT)
      {
        nValue = atoi(szValue);
        pValue = &nValue;
      }
    else if (type == DBGW_VAL_TYPE_LONG)
      {
        lValue = boost::lexical_cast<int64>(szValue);
        pValue = &lValue;
      }

    if (szName == NULL)
      {
        m_parameter.put(type, pValue, bNull);
      }
    else
      {
        m_parameter.put(szName, type, pValue, bNull);
      }
  }

  void DBGWQueryTester::execute(DBGWClient &client)
  {
    DBGWClientResultSetSharedPtr pResult = client.exec(m_sqlName.c_str(), &m_parameter);
    if (m_bDummy)
      {
        return;
      }

    if (getLastErrorCode() != DBGW_ER_NO_ERROR)
      {
        DBGWException e = getLastException();
        if (e.getErrorCode() == DBGW_ER_CLIENT_VALIDATE_TYPE_FAIL)
          {
            fprintf(stderr, "[WARN] %s is failed to execute. %s\n",
                m_sqlName.c_str(), e.what());
            return;
          }
        else
          {
            fprintf(stderr, "[FAIL] %s is failed to execute. %s\n",
                m_sqlName.c_str(), e.what());
            throw e;
          }
      }

    if (pResult->isNeedFetch())
      {
        printf("[OK  ] %s's row count is %d.\n", m_sqlName.c_str(),
            pResult->getRowCount());
      }
    else
      {
        printf("[OK  ] %s's affected row is %d.\n", m_sqlName.c_str(),
            pResult->getAffectedRow());
      }
  }

  bool DBGWQueryTester::isDummy() const
  {
    return m_bDummy;
  }

  const string &DBGWQueryTester::getSqlName() const
  {
    return m_sqlName;
  }

  DBGWQueryTransaction::DBGWQueryTransaction()
  {
  }

  DBGWQueryTransaction::~DBGWQueryTransaction()
  {
  }

  void DBGWQueryTransaction::addQueryTester(DBGWQueryTesterSharedPtr pTester)
  {
    m_testerList.push_back(pTester);
  }

  DBGWQueryTesterList &DBGWQueryTransaction::getTesterList()
  {
    return m_testerList;
  }

  DBGWScenario::DBGWScenario() :
    m_nTestCount(0), m_nPassCount(0)
  {
  }

  DBGWScenario::~DBGWScenario()
  {
  }

  void DBGWScenario::setNamespace(const char *szNamespace)
  {
    m_namespace = szNamespace;
  }

  void DBGWScenario::addQueryTransaction(DBGWQueryTransactionSharedPtr pTransaction)
  {
    m_transactionList.push_back(pTransaction);
  }

  int DBGWScenario::execute(DBGWConfiguration &configuration)
  {
    DBGWClient client(configuration, m_namespace.c_str());
    if (getLastErrorCode() != DBGW_ER_NO_ERROR)
      {
        throw getLastException();
      }

    client.setForceValidateResult();
    client.setAutocommit(false);

    DBGWQueryNameList queryNameList = client.getQueryMapper()->getQueryNameList();

    int nResult = 0;
    int nDummyCount = 0;
    set<string> dummySet;
    set<string> querySet(queryNameList.begin(), queryNameList.end());
    for (DBGWQueryTransactionList::iterator it = m_transactionList.begin(); it
        != m_transactionList.end(); it++)
      {
        DBGWQueryTesterList &testerList = (*it)->getTesterList();
        for (DBGWQueryTesterList::iterator tit = testerList.begin(); tit
            != testerList.end(); tit++)
          {
            if ((*tit)->isDummy())
              {
                dummySet.insert((*tit)->getSqlName());
              }
            querySet.erase((*tit)->getSqlName());

            if (!(*tit)->isDummy())
              {
                ++m_nTestCount;
              }

            try
              {
                (*tit)->execute(client);
                if (!(*tit)->isDummy())
                  {
                    ++m_nPassCount;
                  }
              }
            catch (DBGWException &)
              {
                nResult = 1;
              }
          }
        client.rollback();
      }

    printf("[INFO] %d passed / %d tested in %d querymap.\n", m_nPassCount,
        m_nTestCount, client.getQueryMapper()->size() - dummySet.size());

    if (!querySet.empty())
      {
        string unexecutedQueries;
        set<string>::iterator it = querySet.begin();
        while (it != querySet.end())
          {
            if (unexecutedQueries == "")
              {
                unexecutedQueries = *it;
              }
            else
              {
                unexecutedQueries += ", ";
                unexecutedQueries += *it;
              }
            ++it;
          }

        cout << "[WARN] " << unexecutedQueries << " are not excuted." << endl;
      }

    client.close();

    return nResult;
  }

  DBGWScenarioParser::DBGWScenarioParser(const string &fileName,
      DBGWScenario &scenario) :
    _DBGWParser(fileName), m_scenario(scenario)
  {
  }

  DBGWScenarioParser::~DBGWScenarioParser()
  {
  }

  void DBGWScenarioParser::doOnElementStart(const XML_Char *szName,
      _DBGWExpatXMLProperties &properties)
  {
    if (!strcasecmp(szName, XML_NODE_SCENARIO))
      {
        parseScenario(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_TRANSACTION))
      {
        parseTransaction(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_EXECUTE))
      {
        parseExecute(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_PARAM))
      {
        parseParameter(properties);
      }
  }

  void DBGWScenarioParser::doOnElementEnd(const XML_Char *szName)
  {
    if (!strcasecmp(szName, XML_NODE_EXECUTE))
      {
        m_pTester = DBGWQueryTesterSharedPtr();
      }
    else if (!strcasecmp(szName, XML_NODE_TRANSACTION))
      {
        addTransactionToScenario();
      }
  }

  void DBGWScenarioParser::parseScenario(_DBGWExpatXMLProperties &properties)
  {
    if (isRootElement() == false)
      {
        return;
      }

    m_scenario.setNamespace(properties.get(XML_NODE_SCENARIO_PROP_NAMESPAE, true));
  }

  void DBGWScenarioParser::parseTransaction(_DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_SCENARIO)
      {
        return;
      }

    addTransactionToScenario();
    m_pTransaction = DBGWQueryTransactionSharedPtr(new DBGWQueryTransaction());
  }

  void DBGWScenarioParser::parseExecute(_DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_SCENARIO
        && getParentElementName() != XML_NODE_TRANSACTION)
      {
        return;
      }

    bool bDummy = properties.getBool(XML_NODE_EXECUTE_PROP_DUMMY, false);

    if (m_pTransaction == NULL)
      {
        m_pTransaction = DBGWQueryTransactionSharedPtr(new DBGWQueryTransaction());
      }

    m_pTester = DBGWQueryTesterSharedPtr(
        new DBGWQueryTester(
            properties.get(XML_NODE_EXECUTE_PROP_SQL_NAME, true), bDummy));
    m_pTransaction->addQueryTester(m_pTester);

    if (getParentElementName() != XML_NODE_TRANSACTION && bDummy == false)
      {
        addTransactionToScenario();
      }
  }

  void DBGWScenarioParser::parseParameter(_DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_EXECUTE)
      {
        return;
      }

    if (m_pTester == NULL)
      {
        return;
      }

    m_pTester->addParameter(
        properties.getCString(XML_NODE_PARAM_PROP_NAME, false),
        properties.get30ValueType(XML_NODE_PARAM_PROP_TYPE),
        properties.get(XML_NODE_PARAM_PROP_VALUE, false),
        properties.getBool(XML_NODE_PARAM_PROP_ISNULL, false));
  }

  void DBGWScenarioParser::addTransactionToScenario()
  {
    if (m_pTransaction != NULL)
      {
        m_scenario.addQueryTransaction(m_pTransaction);
        m_pTransaction = DBGWQueryTransactionSharedPtr();
      }
  }

}

using namespace dbgw;

const static int PROG_MIN_ARG_COUNT = 4;
const static char *PROG_USAGE =
    "dbgw_query_tester [scenario xml path] [configuration xml path] [querymap xml path] ...";

int main(int argc, const char *argv[])
{
  if (argc < PROG_MIN_ARG_COUNT)
    {
      cerr << PROG_USAGE << endl;
      return 1;
    }

  DBGWConfiguration configuration(argv[2]);
  if (getLastErrorCode() != DBGW_ER_NO_ERROR)
    {
      cerr << getLastException().what() << endl;
      return 1;
    }

  try
    {
      DBGWScenario scenario;
      DBGWScenarioParser parser(argv[1], scenario);
      _DBGWParser::parse(&parser);

      for (int i = 3; i < argc; i++)
        {
          if (configuration.loadQueryMapper(DBGW_QUERY_MAP_VER_30, argv[i],
              true) == false)
            {
              throw getLastException();
            }
        }

      return scenario.execute(configuration);
    }
  catch (DBGWException &e)
    {
      cerr << "[FAIL] " << e.what() << endl;
      return 1;
    }
}
