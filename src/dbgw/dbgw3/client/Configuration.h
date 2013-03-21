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

#ifndef CONFIGURATION_H_
#define CONFIGURATION_H_

namespace dbgw
{

  class _AsyncWorker;
  class _QueryMapper;
  class _Service;
  class _StatisticsMonitor;
  class _Timer;

  enum QueryMapperVersion
  {
    DBGW_QUERY_MAP_VER_UNKNOWN,
    DBGW_QUERY_MAP_VER_10,
    DBGW_QUERY_MAP_VER_20,
    DBGW_QUERY_MAP_VER_30
  };

  struct _ConfigurationVersion
  {
    int nConnectorVersion;
    int nQueryMapperVersion;
  };

  class Configuration : public _SynchronizedResourceSubject
  {
  public:
    static unsigned long DEFAULT_MAX_WAIT_EXIT_TIME_MILSEC();

  public:
    Configuration();
    Configuration(const char *szConfFileName, bool bNeedLoadXml = true);
    virtual ~ Configuration();

    void setWaitTimeMilSec(unsigned long ulWaitTimeMilSec);
    void setMaxWaitExitTimeMilSec(unsigned long ulMaxWaitExitTimeMilSec);
    bool loadConfiguration();
    bool loadConnector(const char *szXmlPath = NULL);
    bool loadQueryMapper();
    bool loadQueryMapper(const char *szXmlPath, bool bAppend = false);
    bool loadQueryMapper(QueryMapperVersion version, const char *szXmlPath,
        bool bAppend = false);
    bool closeVersion(const _ConfigurationVersion &stVersion);
    _ConfigurationVersion getVersion();
    trait<_Service>::sp getService(const _ConfigurationVersion &stVersion,
        const char *szNameSpace);
    trait<_QueryMapper>::sp getQueryMapper(const _ConfigurationVersion &stVersion);
    trait<_AsyncWorker>::sp getAsyncWorker();

  public:
    unsigned long getWaitTimeMilSec() const;
    unsigned long getMaxWaitExitTimeMilSec() const;
    int getConnectorSize() const;
    int getQueryMapperSize() const;
    trait<_StatisticsMonitor>::sp getMonitor() const;
    trait<_Timer>::sp getTimer() const;

  private:
    class Impl;
    Impl *m_pImpl;
  };

}

#endif
