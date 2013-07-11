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

#ifndef CLIENT_H_
#define CLIENT_H_

#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Lob.h"
#include "dbgw3/Value.h"
#include "dbgw3/ValueSet.h"
#include "dbgw3/SynchronizedResource.h"
#include "dbgw3/system/ThreadEx.h"
#include "dbgw3/client/ClientResultSet.h"
#include "dbgw3/client/Interface.h"
#include "dbgw3/client/ConfigurationObject.h"
#include "dbgw3/client/Configuration.h"
#include "dbgw3/client/Resource.h"
#include "dbgw3/client/QueryMapper.h"

namespace dbgw
{

  class Configuration;

  typedef void (*ExecAsyncCallBack)(int nHandleId,
      trait<ClientResultSet>::sp, const Exception &, void *);

  typedef void (*ExecBatchAsyncCallBack)(int nHandleId,
      trait<ClientResultSet>::spvector, const Exception &, void *);

  class Client : public _SynchronizedResource
  {
  public:
    Client(Configuration &configuration, const char *szNameSpace = NULL);
    virtual ~Client();

    void setWaitTimeMilSec(unsigned long ulWaitTimeMilSec);
    bool setForceValidateResult();
    bool setAutocommit(bool bIsAutocommit);
    bool setAutocommit(bool bIsAutocommit, unsigned long ulWaitTimeMilSec);
    bool commit();
    bool commit(unsigned long ulWaitTimeMilSec);
    bool rollback();
    bool rollback(unsigned long ulWaitTimeMilSec);
    trait<ClientResultSet>::sp exec(const char *szSqlName,
        unsigned long ulWaitTimeMilSec);
    trait<ClientResultSet>::sp exec(const char *szSqlName,
        const _Parameter *pParameter = NULL);
    trait<ClientResultSet>::sp exec(const char *szSqlName,
        const _Parameter *pParameter, unsigned long ulWaitTimeMilSec);
    int execAsync(const char *szSqlName, ExecAsyncCallBack pCallBack,
        void *pData = NULL);
    int execAsync(const char *szSqlName, ExecAsyncCallBack pCallBack,
        unsigned long ulWaitTimeMilSec, void *pData = NULL);
    int execAsync(const char *szSqlName, const _Parameter *pParameter,
        ExecAsyncCallBack pCallBack, void *pData = NULL);
    int execAsync(const char *szSqlName, const _Parameter *pParameter,
        ExecAsyncCallBack pCallBack, unsigned long ulWaitTimeMilSec,
        void *pData = NULL);
    trait<ClientResultSet>::spvector execBatch(const char *szSqlName,
        const _ParameterList &parameterList);
    trait<ClientResultSet>::spvector execBatch(const char *szSqlName,
        const _ParameterList &parameterList,
        unsigned long ulWaitTimeMilSec);
    int execBatchAsync(const char *szSqlName,
        const _ParameterList &parameterList,
        ExecBatchAsyncCallBack pCallBack, void *pData = NULL);
    int execBatchAsync(const char *szSqlName,
        const _ParameterList &parameterList,
        ExecBatchAsyncCallBack pCallBack, unsigned long ulWaitTimeMilSec,
        void *pData = NULL);
    bool close();
    trait<Lob>::sp createClob();
    trait<Lob>::sp createClob(unsigned long ulWaitTimeMilSec);
    trait<Lob>::sp createBlob();
    trait<Lob>::sp createBlob(unsigned long ulWaitTimeMilSec);

  public:
    bool isClosed() const;
    bool isAutocommit() const;
    const _QueryMapper *getQueryMapper() const;
    unsigned long getWaitTimeMilSec() const;

  protected:
    virtual void doUnlinkResource();

  private:
    class Impl;
    Impl *m_pImpl;

  private:
    Client(const Client &);
    Client &operator=(const Client &);

  private:
    static const char *szVersionString;
  };

}

#endif
