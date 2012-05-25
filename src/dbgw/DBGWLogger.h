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

  class DBGWLogger
  {
  public:
    DBGWLogger(const string &sqlName);
    DBGWLogger(const string &groupName, const string &sqlName);
    virtual ~ DBGWLogger();

  public:
    const string getLogMessage(const char *szMsg) const;

  public:
    static void initialize();
    static void initialize(CCI_LOG_LEVEL level, const char *szLogPath);
    static void setLogPath(const char *szLogPath);
    static void setLogLevel(CCI_LOG_LEVEL level);
    static void setForceFlush(bool bForceFlush);
    static void finalize();
    static void write(const char *szFile, int nLine, CCI_LOG_LEVEL level,
        const char *szFormat, ...);
    static const char *getLogPath();

  private:
    string m_groupName;
    string m_sqlName;
    static Logger m_logger;
    static string m_logPath;
  };

#define DBGW_LOG_ERROR(...) \
  do { \
    DBGWLogger::write(__FILE__, __LINE__, CCI_LOG_LEVEL_ERROR, __VA_ARGS__); \
  } while (false)

#define DBGW_LOG_WARN(...) \
  do { \
    DBGWLogger::write(__FILE__, __LINE__, CCI_LOG_LEVEL_WARN, __VA_ARGS__); \
  } while (false)

#define DBGW_LOG_INFO(...) \
  do { \
    DBGWLogger::write(__FILE__, __LINE__, CCI_LOG_LEVEL_INFO, __VA_ARGS__); \
  } while (false)

#define DBGW_LOG_DEBUG(...) \
  do { \
    DBGWLogger::write(__FILE__, __LINE__, CCI_LOG_LEVEL_DEBUG, __VA_ARGS__); \
  } while (false)

}

#endif				/* DBGWLOGGER_H_ */
