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

#ifndef DBGWQUERYTESTER_H_
#define DBGWQUERYTESTER_H_

namespace dbgw
{

  class DBGWQueryTester
  {
  public:
    DBGWQueryTester(const char *szSqlName, bool bDummy);
    virtual ~DBGWQueryTester();

    void addParameter(const char *szName, DBGWValueType type,
        const char *szValue, bool bNull);
    void execute(DBGWClient &client);

  public:
    bool isDummy() const;
    const string &getSqlName() const;

  private:
    string m_sqlName;
    bool m_bDummy;
    _DBGWParameter m_parameter;
  };

  typedef boost::shared_ptr<DBGWQueryTester> DBGWQueryTesterSharedPtr;
  typedef list<DBGWQueryTesterSharedPtr> DBGWQueryTesterList;

  class DBGWQueryTransaction
  {
  public:
    DBGWQueryTransaction();
    virtual ~DBGWQueryTransaction();

    void addQueryTester(DBGWQueryTesterSharedPtr pTester);
    DBGWQueryTesterList &getTesterList();

  private:
    DBGWQueryTesterList m_testerList;
  };

  typedef boost::shared_ptr<DBGWQueryTransaction> DBGWQueryTransactionSharedPtr;
  typedef list<DBGWQueryTransactionSharedPtr> DBGWQueryTransactionList;

  class DBGWScenario
  {
  public:
    DBGWScenario();
    virtual ~DBGWScenario();

    void setNamespace(const char *szNamespace);
    void addQueryTransaction(DBGWQueryTransactionSharedPtr pTransaction);
    int execute(DBGWConfiguration &configuration);

  private:
    string m_namespace;
    DBGWQueryTransactionList m_transactionList;
    int m_nTestCount;
    int m_nPassCount;
  };

  class DBGWScenarioParser: public _DBGWParser
  {
  public:
    DBGWScenarioParser(const string &fileName, DBGWScenario &scenario);
    virtual ~ DBGWScenarioParser();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        _DBGWExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);

  private:
    void parseScenario(_DBGWExpatXMLProperties &properties);
    void parseTransaction(_DBGWExpatXMLProperties &properties);
    void parseExecute(_DBGWExpatXMLProperties &properties);
    void parseParameter(_DBGWExpatXMLProperties &properties);
    void addTransactionToScenario();

  private:
    DBGWScenario &m_scenario;
    DBGWQueryTesterSharedPtr m_pTester;
    DBGWQueryTransactionSharedPtr m_pTransaction;
  };

}

extern int main(int argc, const char *argv[]);

#endif /* DBGWQUERYTESTER_H_ */
