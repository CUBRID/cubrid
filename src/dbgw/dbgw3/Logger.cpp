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
#include "dbgw3/Common.h"
#include "dbgw3/Logger.h"
#include "dbgw3/system/Mutex.h"

namespace dbgw
{

  static const char *DBGW_LOG_PATH = "log/cci_dbgw.log";
  static const int LOG_BUFFER_SIZE = 1024 * 20;
  static system::_Mutex g_logMutex;

  Logger _Logger::m_logger = NULL;
  std::string _Logger::m_logPath = DBGW_LOG_PATH;
  CCI_LOG_LEVEL _Logger::m_logLevel = CCI_LOG_LEVEL_ERROR;

  _Logger::_Logger()
  {
  }

  _Logger::_Logger(const std::string &sqlName) :
    m_sqlName(sqlName)
  {
  }

  _Logger::_Logger(const std::string &groupName, const std::string &sqlName) :
    m_groupName(groupName), m_sqlName(sqlName)
  {
  }

  _Logger::~_Logger()
  {
  }

  void _Logger::setGroupName(const std::string &groupName)
  {
    m_groupName = groupName;
  }

  void _Logger::setSqlName(const std::string &sqlName)
  {
    m_sqlName = sqlName;
  }

  const std::string _Logger::getLogMessage(const char *szMsg) const
  {
    std::string message;
    if (m_groupName == "")
      {
        message += m_sqlName;
        message += " : ";
        message += szMsg;
      }
    else if (m_sqlName == "")
      {
        message += m_groupName;
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

  void _Logger::initialize()
  {
    initialize(m_logLevel, DBGW_LOG_PATH);
  }

  void _Logger::initialize(CCI_LOG_LEVEL level, const std::string &logPath)
  {
    setLogPath(logPath);
    setLogLevel(level);
  }

  void _Logger::setLogPath(const std::string &logPath)
  {
    system::_MutexAutoLock lock(&g_logMutex);

    if (logPath != "")
      {
        m_logPath = logPath;
        m_logger = cci_log_get(m_logPath.c_str());
      }
  }

  void _Logger::setLogLevel(CCI_LOG_LEVEL level)
  {
    system::_MutexAutoLock lock(&g_logMutex);

    if (m_logger != NULL)
      {
        cci_log_set_level(m_logger, level);
      }
  }

  void _Logger::setForceFlush(bool bForceFlush)
  {
    system::_MutexAutoLock lock(&g_logMutex);

    if (m_logger != NULL)
      {
        cci_log_set_force_flush(m_logger, bForceFlush);
      }
  }

  void _Logger::setDefaultPostfix(CCI_LOG_POSTFIX postfix)
  {
    system::_MutexAutoLock lock(&g_logMutex);

    if (m_logger != NULL)
      {
        cci_log_set_default_postfix(m_logger, postfix);
      }
  }

  void _Logger::finalize()
  {
    system::_MutexAutoLock lock(&g_logMutex);

    if (m_logger != NULL)
      {
        cci_log_remove(m_logPath.c_str());
        m_logger = NULL;
      }
  }

  void _Logger::writeLogF(const char *szFile, int nLine, CCI_LOG_LEVEL level,
      const char *szFormat, ...)
  {
    system::_MutexAutoLock lock(&g_logMutex);

    if (m_logger != NULL)
      {
        const char *szBase = strrchr(szFile, '/');
        szBase = szBase ? (szBase + 1) : szFile;
        char fileLineBuf[64];
        snprintf(fileLineBuf, 64, "%s:%d", szBase, nLine);

        char prefixBuf[LOG_BUFFER_SIZE];
        snprintf(prefixBuf, LOG_BUFFER_SIZE, " %-40s %s", fileLineBuf, szFormat);

        char szLogFormat[LOG_BUFFER_SIZE];
        va_list vl;
        va_start(vl, szFormat);
        vsnprintf(szLogFormat, LOG_BUFFER_SIZE, prefixBuf, vl);
        va_end(vl);

        if (cci_log_is_writable(m_logger, level))
          {
            cci_log_write(level, m_logger, szLogFormat);
          }
      }
  }

  void _Logger::writeLog(const char *szFile, int nLine, CCI_LOG_LEVEL level,
      const char *szLog)
  {
    system::_MutexAutoLock lock(&g_logMutex);

    if (m_logger != NULL)
      {
        const char *szBase = strrchr(szFile, '/');
        szBase = szBase ? (szBase + 1) : szFile;
        char fileLineBuf[64];
        snprintf(fileLineBuf, 64, "%s:%d", szBase, nLine);

        char logBuf[LOG_BUFFER_SIZE];
        snprintf(logBuf, LOG_BUFFER_SIZE, " %-40s %s", fileLineBuf, szLog);

        if (cci_log_is_writable(m_logger, level))
          {
            cci_log_write(level, m_logger, logBuf);
          }
      }
  }

  bool _Logger::isWritable(CCI_LOG_LEVEL level)
  {
    return cci_log_is_writable(m_logger, level);
  }

  const char *_Logger::getLogPath()
  {
    return m_logPath.c_str();
  }

  CCI_LOG_LEVEL _Logger::getLogLevel()
  {
    return m_logLevel;
  }

  _LogDecorator::_LogDecorator(const char *szHeader) :
    m_header(szHeader), m_iLogCount(0)
  {
    clear();
  }

  _LogDecorator::~_LogDecorator()
  {
  }

  void _LogDecorator::clear()
  {
    m_iLogCount = 0;
    m_buffer.str("");

    m_buffer << m_header;
    m_buffer << " [";
  }

  void _LogDecorator::addLog(const std::string &log)
  {
    if (m_iLogCount++ > 0)
      {
        m_buffer << ", ";
      }
    m_buffer << log;
  }

  void _LogDecorator::addLogDesc(const std::string &desc)
  {
    m_buffer << "(" << desc << ")";
  }

  std::string _LogDecorator::getLog()
  {
    m_buffer << "]";
    return m_buffer.str();
  }

}
