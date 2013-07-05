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
#ifndef DBGWLOGGER_H_
#define DBGWLOGGER_H_

namespace dbgw
{

  class _Logger
  {
  public:
    _Logger();
    _Logger(const std::string &sqlName);
    _Logger(const std::string &groupName, const std::string &sqlName);
    virtual ~ _Logger();

    void setGroupName(const std::string &groupName);
    void setSqlName(const std::string &sqlName);

  public:
    const std::string getLogMessage(const char *szMsg) const;

  public:
    static void initialize();
    static void initialize(CCI_LOG_LEVEL level, const std::string &logPath);
    static void setLogPath(const std::string &logPath);
    static void setLogLevel(CCI_LOG_LEVEL level);
    static void setForceFlush(bool bForceFlush);
    static void setDefaultPostfix(CCI_LOG_POSTFIX postfix);
    static void finalize();
    static void writeLogF(const char *szFile, int nLine, CCI_LOG_LEVEL level,
        const char *szFormat, ...);
    static void writeLog(const char *szFile, int nLine, CCI_LOG_LEVEL level,
        const char *szLog);
    static bool isWritable(CCI_LOG_LEVEL level);
    static const char *getLogPath();
    static CCI_LOG_LEVEL getLogLevel();

  private:
    std::string m_groupName;
    std::string m_sqlName;
    static Logger m_logger;
    static std::string m_logPath;
    static CCI_LOG_LEVEL m_logLevel;
  };

  class _LogDecorator
  {
  public:
    _LogDecorator(const char *szHeader);
    virtual ~ _LogDecorator();

    void clear();
    void addLog(const std::string &log);
    void addLogDesc(const std::string &desc);
    std::string getLog();

  protected:
    std::stringstream m_buffer;
    std::string m_header;
    int m_iLogCount;
  };

#define DBGW_LOG_ERROR(LOG) \
  do { \
    if (dbgw::_Logger::isWritable(CCI_LOG_LEVEL_ERROR)) { \
      dbgw::_Logger::writeLog(__FILENAME__, __LINE__, CCI_LOG_LEVEL_ERROR, LOG); \
    } \
  } while (false)

#define DBGW_LOGF_ERROR(...) \
  do { \
    if (dbgw::_Logger::isWritable(CCI_LOG_LEVEL_ERROR)) { \
      dbgw::_Logger::writeLogF(__FILENAME__, __LINE__, CCI_LOG_LEVEL_ERROR, __VA_ARGS__); \
    } \
  } while (false)

#define DBGW_LOG_WARN(LOG) \
  do { \
    if (dbgw::_Logger::isWritable(CCI_LOG_LEVEL_WARN)) { \
      dbgw::_Logger::writeLog(__FILENAME__, __LINE__, CCI_LOG_LEVEL_WARN, LOG); \
    } \
  } while (false)

#define DBGW_LOGF_WARN(...) \
  do { \
    if (dbgw::_Logger::isWritable(CCI_LOG_LEVEL_WARN)) { \
      dbgw::_Logger::writeLogF(__FILENAME__, __LINE__, CCI_LOG_LEVEL_WARN, __VA_ARGS__); \
    } \
  } while (false)

#define DBGW_LOG_INFO(LOG) \
  do { \
    if (dbgw::_Logger::isWritable(CCI_LOG_LEVEL_INFO)) { \
      dbgw::_Logger::writeLog(__FILENAME__, __LINE__, CCI_LOG_LEVEL_INFO, LOG); \
    } \
  } while (false)

#define DBGW_LOGF_INFO(...) \
  do { \
    if (dbgw::_Logger::isWritable(CCI_LOG_LEVEL_INFO)) { \
      dbgw::_Logger::writeLogF(__FILENAME__, __LINE__, CCI_LOG_LEVEL_INFO, __VA_ARGS__); \
    } \
  } while (false)

#define DBGW_LOG_DEBUG(LOG) \
  do { \
    if (dbgw::_Logger::isWritable(CCI_LOG_LEVEL_DEBUG)) { \
      dbgw::_Logger::writeLog(__FILENAME__, __LINE__, CCI_LOG_LEVEL_DEBUG, LOG); \
    } \
  } while (false)

#define DBGW_LOGF_DEBUG(...) \
  do { \
    if (dbgw::_Logger::isWritable(CCI_LOG_LEVEL_DEBUG)) { \
      dbgw::_Logger::writeLogF(__FILENAME__, __LINE__, CCI_LOG_LEVEL_DEBUG, __VA_ARGS__); \
    } \
  } while (false)

}

#endif				/* DBGWLOGGER_H_ */
