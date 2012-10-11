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
#ifndef DBGWCLIENT_H_
#define DBGWCLIENT_H_

#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWPorting.h"
#include "DBGWValue.h"
#include "DBGWLogger.h"
#include "DBGWQuery.h"
#include "DBGWPorting.h"
#include "DBGWDataBaseInterface.h"
#include "DBGWConfiguration.h"

namespace dbgw
{
  typedef boost::unordered_map<string, DBGWResultSharedPtr,
          boost::hash<string>, dbgwStringCompareFunc> DBGWResultHashMap;

  /**
   * External access class.
   */
  class DBGWClient
  {
  public:
    DBGWClient(DBGWConfiguration &configuration, const char *szNamespace = NULL);
    virtual ~ DBGWClient();

    bool setForceValidateResult();
    bool setAutocommit(bool bAutocommit);
    bool commit();
    bool rollback();
    const DBGWResultSharedPtr exec(const char *szSqlName,
        const DBGWParameter *pParameter = NULL);
    bool close();

  public:
    bool isClosed() const;
    bool isAutocommit() const;
    const DBGWQueryMapper *getQueryMapper() const;

  private:
    void validateResult(const DBGWLogger &logger, DBGWResultSharedPtr pReturnResult,
        DBGWResultSharedPtr pInternalResult);
    void checkClientIsValid();

  private:
    DBGWConfiguration &m_configuration;
    DBGWConfigurationVersion m_stVersion;
    DBGWConnector *m_pConnector;
    DBGWQueryMapper *m_pQueryMapper;
    DBGWExecutorList m_executorList;
    char *m_szNamespace;
    bool m_bClosed;
    bool m_bValidClient;
    bool m_bAutocommit;

  private:
    static const char *szVersionString;
  };

}

#endif				/* DBGWCLIENT_H_ */
