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

#ifndef GROUP_H_
#define GROUP_H_

namespace dbgw
{

  enum _ExecutorPoolStatColumn
  {
    DBGW_CONN_POOL_STAT_COL_PADDING = 0,
    DBGW_CONN_POOL_STAT_COL_NAMESPACE,
    DBGW_CONN_POOL_STAT_COL_GROUPNAME,
    DBGW_CONN_POOL_STAT_COL_ACTIVE_CNT,
    DBGW_CONN_POOL_STAT_COL_IDLE_CNT,
    DBGW_CONN_POOL_STAT_COL_SUCC_CNT,
    DBGW_CONN_POOL_STAT_COL_FAIL_CNT,
    DBGW_CONN_POOL_STAT_COL_EVICT_CNT
  };

  enum _ExecutorStatementStatColumn
  {
    DBGW_STMT_POOL_STAT_COL_PADDING = 0,
    DBGW_STMT_POOL_STAT_COL_GROUPNAME,
    DBGW_STMT_POOL_STAT_COL_TOTAL_CNT,
    DBGW_STMT_POOL_STAT_COL_GET_CNT,
    DBGW_STMT_POOL_STAT_COL_HIT_CNT,
    DBGW_STMT_POOL_STAT_COL_EVICT_CNT,
    DBGW_STMT_POOL_STAT_COL_HIT_RATIO
  };

  class _Service;
  class _Host;
  class _Executor;
  class _StatisticsItem;

  struct _ExecutorPoolContext
  {
    _ExecutorPoolContext();

    static size_t DEFAULT_INITIAL_SIZE();
    static int DEFAULT_MIN_IDLE();
    static int DEFAULT_MAX_IDLE();
    static int DEFAULT_MAX_ACTIVE();
    static unsigned long DEFAULT_TIME_BETWEEN_EVICTION_RUNS_MILLIS();
    static int DEFAULT_NUM_TESTS_PER_EVICTIONRUN();
    static unsigned long DEFAULT_MIN_EVICTABLE_IDLE_TIMEMILLIS();
    static sql::TransactionIsolarion DEFAULT_ISOLATION();
    static bool DEFAULT_AUTOCOMMIT();

    size_t initialSize;
    int minIdle;
    int maxIdle;
    int maxActive;
    int currActive;
    unsigned long timeBetweenEvictionRunsMillis;
    int numTestsPerEvictionRun;
    unsigned long minEvictableIdleTimeMillis;
    sql::TransactionIsolarion isolation;
    bool autocommit;
  };

  class _Group : public _ConfigurationObject
  {
  public:
    _Group(_Service *pService, const std::string &fileName,
        const std::string &name, const std::string &description, bool bInactivate,
        bool bIgnoreResult, bool useDefaultValueWhenFailedToCastParam,
        int nMaxPreparedStatementSize, CodePage dbCodePage,
        CodePage clientCodePage);
    virtual ~_Group();

    void initPool(const _ExecutorPoolContext &context);
    void evictUnsuedExecutor();
    trait<_Executor>::sp getExecutor();
    void returnExecutor(trait<_Executor>::sp pExecutor,
        bool bIsDetached = false);
    _CharsetConverter *getDBCharesetConverter();
    _CharsetConverter *getClientCharesetConverter();

  public:
    void addHost(trait<_Host>::sp pHost);

  public:
    const std::string &getFileName() const;
    const std::string &getName() const;
    const std::string &getNameSpace() const;
    bool isInactivate() const;
    bool isIgnoreResult() const;
    bool isUseDefaultValueWhenFailedToCastParam() const;
    bool empty() const;
    _StatisticsItem &getStatementStatItem();

    class Impl;
    Impl *m_pImpl;
  };

}

#endif
