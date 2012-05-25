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
#include <sys/stat.h>
#include <fstream>
#include <stdarg.h>
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWLogger.h"

namespace dbgw
{

  static const char *DBGW_LOG_PATH = "log/cci_dbgw.log";
  static const int LOG_BUFFER_SIZE = 1024 * 20;
  static Mutex g_logMutex;

  Logger DBGWLogger::m_logger = NULL;
  string DBGWLogger::m_logPath = DBGW_LOG_PATH;

  DBGWLogger::DBGWLogger(const string &sqlName) :
    m_sqlName(sqlName)
  {
  }

  DBGWLogger::DBGWLogger(const string &groupName, const string &sqlName) :
    m_groupName(groupName), m_sqlName(sqlName)
  {
  }

  DBGWLogger::~DBGWLogger()
  {
  }

  const string DBGWLogger::getLogMessage(const char *szMsg) const
  {
    string message;
    if (m_groupName == "")
      {
        message += m_sqlName;
        message += " : ";
        message += szMsg;
      }
    else
      {
        message += m_groupName;
        message += ".";
        message += m_sqlName;
        message += " : ";
        message += szMsg;
      }
    return message;
  }

  void DBGWLogger::initialize()
  {
    initialize(CCI_LOG_LEVEL_INFO, DBGW_LOG_PATH);
  }

  void DBGWLogger::initialize(CCI_LOG_LEVEL level, const char *szLogPath)
  {
    setLogPath(szLogPath);
    setLogLevel(level);
  }

  void DBGWLogger::setLogPath(const char *szLogPath)
  {
    g_logMutex.lock();
    if (szLogPath != NULL)
      {
        if (m_logger != NULL)
          {
            cci_log_remove(m_logPath.c_str());
          }

        m_logPath = szLogPath;
        m_logger = cci_log_get(m_logPath.c_str());
      }
    g_logMutex.unlock();
  }

  void DBGWLogger::setLogLevel(CCI_LOG_LEVEL level)
  {
    if (m_logger != NULL)
      {
        cci_log_set_level(m_logger, level);
      }
  }

  void DBGWLogger::setForceFlush(bool bForceFlush)
  {
    if (m_logger != NULL)
      {
        cci_log_set_force_flush(m_logger, bForceFlush);
      }
  }

  void DBGWLogger::finalize()
  {
    g_logMutex.lock();
    if (m_logger != NULL)
      {
        cci_log_remove(m_logPath.c_str());
        m_logger = NULL;
      }
    g_logMutex.unlock();
  }

  void DBGWLogger::write(const char *szFile, int nLine, CCI_LOG_LEVEL level,
      const char *szFormat, ...)
  {
    const char *szBase = strrchr(szFile, '/');
    szBase = szBase ? (szBase + 1) : szFile;
    char fileLineBuf[32];
    snprintf(fileLineBuf, 32, "%s:%d", szBase, nLine);

    char prefixBuf[LOG_BUFFER_SIZE];
    snprintf(prefixBuf, LOG_BUFFER_SIZE, " %-31s %s", fileLineBuf, szFormat);

    char szLogFormat[LOG_BUFFER_SIZE];
    va_list vl;
    va_start(vl, szFormat);
    vsnprintf(szLogFormat, LOG_BUFFER_SIZE, prefixBuf, vl);
    va_end(vl);

    if (m_logger != NULL)
      {
        cci_log_write(level, m_logger, szLogFormat);
      }
  }

  const char *DBGWLogger::getLogPath()
  {
    return m_logPath.c_str();
  }

}
