/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

/*
 * cci_log.cpp -
 */

#include <errno.h>
#include <stdarg.h>
#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <time.h>
#include <direct.h>
#else
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <iostream>
#include <fstream>
#include <string>
#include <map>

#include "cci_common.h"
#include "cci_log.h"

static const int LOG_BUFFER_SIZE = 1024 * 20;
static const int LOG_ER_OPEN = -1;
static const long int LOG_FLUSH_SIZE = 1024 * 1024; /* byte */
static const long int LOG_FLUSH_USEC = 1 * 1000000; /* usec */
static const char *cciLogLevelStr[] =
{ "OFF", "ERROR", "WARN", "INFO", "DEBUG" };

using namespace std;

namespace cci
{
  class _Mutex
  {
  private:
    pthread_mutex_t mutex;

  public:
    _Mutex()
    {
      pthread_mutex_init(&mutex, NULL);
    }

    ~_Mutex()
    {
      pthread_mutex_destroy(&mutex);
    }

    int lock()
    {
      return pthread_mutex_lock(&mutex);
    }

    int unlock()
    {
      return pthread_mutex_unlock(&mutex);
    }
  };
}

class _Logger
{
public:
  _Logger(const char *path) :
    base(path), roleTime(time(0)), level(CCI_LOG_LEVEL_INFO),
    unflushedBytes(0), nextFlushTime(0), forceFlush(false)
  {
  }

  virtual ~_Logger()
  {
    critical.lock();
    flush();
    critical.unlock();
    if (out.is_open())
      {
        out.close();
      }
  }

  void open(void)
  {
    out.open(base.c_str(), fstream::out | fstream::app);
    if (out.fail())
      {
        makeLogDir();

        out.open(base.c_str(), fstream::out | fstream::app);
        if (out.fail())
          {
            throw LOG_ER_OPEN;
          }
      }
  }

  void setLevel(CCI_LOG_LEVEL level)
  {
    this->level = level;
  }

  void log(CCI_LOG_LEVEL level, char *msg)
  {
    if (!out.is_open())
      {
        return;
      }

    dailyRole();
    logInternal(level, msg);
  }

  void flush()
  {
    if (!out.is_open())
      {
        return;
      }

    out.flush();
    unflushedBytes = 0;

    nextFlushTime = now() + LOG_FLUSH_USEC;
  }

  bool isLoggerWritable(CCI_LOG_LEVEL level)
  {
    return this->level >= level;
  }

  void setForceFlush(bool forceFlush)
  {
    this->forceFlush = forceFlush;
  }

private:
  void write(const char *msg)
  {
    if (!out.is_open())
      {
        return;
      }

    out << msg;

    unflushedBytes += strlen(msg);

    if (forceFlush || unflushedBytes >= LOG_FLUSH_SIZE || now()
        >= nextFlushTime)
      {
        flush();
      }
  }

  void logPrefix(CCI_LOG_LEVEL level)
  {
    struct timeval tv;
    struct tm cal;

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &cal);
    cal.tm_year += 1900;
    cal.tm_mon += 1;

    char buf[128];
    unsigned long tid = gettid();
    snprintf(buf, 128, "%d-%02d-%02d %02d:%02d:%02d.%03d [TID:%lu] [%5s]",
        cal.tm_year, cal.tm_mon, cal.tm_mday, cal.tm_hour, cal.tm_min,
        cal.tm_sec, (int)(tv.tv_usec / 1000), tid,
        cciLogLevelStr[level]);

    write(buf);
  }

  int logInternal(CCI_LOG_LEVEL level, char *msg)
  {
    critical.lock();
    logPrefix(level);
    write(msg);
    write("\n");
    critical.unlock();
    return 0;
  }

  bool isRole(int secs)
  {
    time_t role = roleTime / secs * secs;
    time_t now = time(0);

    now = now / secs * secs;
    return role != now;
  }

  string getDate(void)
  {
    struct tm cal;
    char buf[16];

    localtime_r(&roleTime, &cal);
    cal.tm_year += 1900;
    cal.tm_mon += 1;
    snprintf(buf, 16, "%d-%02d-%02d", cal.tm_year, cal.tm_mon, cal.tm_mday);
    return buf;
  }

  void role(void)
  {
    out.close();
    string rolePath = base + "." + getDate();
    int e = rename(base.c_str(), rolePath.c_str());
    if (e != 0)
      {
        perror("rename");
      }
    open();
  }

  void hourRole()
  {
    critical.lock();
    if (!isRole(3600))
      {
        critical.unlock();
        return;
      }

    role();
    roleTime += 3600;
    critical.unlock();
  }

  void dailyRole()
  {
    critical.lock();
    if (!isRole(86400))
      {
        critical.unlock();
        return;
      }

    role();
    roleTime += 86400;
    critical.unlock();
  }

  void makeLogDir()
  {
    const char *sep = "/\\";

    char dir[FILENAME_MAX];
    char *p = dir;
    const char *q = base.c_str();

    while (*q)
      {
        *p++ = *q;
        *p = '\0';
        if (*q == sep[0] || *q == sep[1])
          {
            mkdir(dir, 0755);
          }
        q++;
      }
  }

  long int now()
  {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_usec;
  }

private:
  ofstream out;
  cci::_Mutex critical;
  string base;
  time_t roleTime;
  CCI_LOG_LEVEL level;
  long int unflushedBytes;
  long int nextFlushTime;
  bool forceFlush;
};

typedef map<string, _Logger *> MapPathLogger;
typedef MapPathLogger::iterator IteratorPathLogger;

class _LogManager
{
public:
  _LogManager()
  {
  }

  virtual ~_LogManager()
  {
    clear();
  }

  Logger getLog(const char *path)
  {
    IteratorPathLogger it = loggers.find(path);
    if (it == loggers.end())
      {
        return addLog(path);
      }

    return it->second;
  }

  Logger addLog(const char *path)
  {
    _Logger *logger = NULL;

    try
      {
        logger = new _Logger(path);
        logger->open();
      }
    catch (...)
      {
        if (logger != NULL)
          {
            delete logger;
          }
        return NULL;
      }

    if (logger != NULL)
      {
        critical.lock();
        loggers[path] = logger;
        critical.unlock();
      }

    return logger;
  }

  void removeLog(const char *path)
  {
    critical.lock();
    IteratorPathLogger it = loggers.find(path);
    if (it != loggers.end())
      {
        delete it->second;
        loggers.erase(it);
      }
    critical.unlock();
  }

  void clear()
  {
    critical.lock();
    IteratorPathLogger it = loggers.begin();
    while (it != loggers.end())
      {
        if (it->second != NULL)
          {
            delete it->second;
          }
        ++it;
      }
    loggers.clear();
    critical.unlock();
  }

private:
  MapPathLogger loggers;
  cci::_Mutex critical;
};

static _LogManager logManager;

Logger cci_log_add(const char *path)
{
  return logManager.addLog(path);
}

Logger cci_log_get(const char *path)
{
  return logManager.getLog(path);
}

void cci_log_finalize(void)
{
  logManager.clear();
}

void cci_log_writef(CCI_LOG_LEVEL level, Logger logger, const char *format, ...)
{
  _Logger *l = (_Logger *) logger;

  if (l == NULL)
    {
      return;
    }

  if (l->isLoggerWritable(level))
    {
      char buf[LOG_BUFFER_SIZE];
      va_list vl;

      va_start(vl, format);
      vsnprintf(buf, LOG_BUFFER_SIZE, format, vl);
      va_end(vl);

      l->log(level, buf);
    }
}

void cci_log_write(CCI_LOG_LEVEL level, Logger logger, const char *log)
{
  _Logger *l = (_Logger *) logger;

  if (l == NULL)
    {
      return;
    }

  if (l->isLoggerWritable(level))
    {
      l->log(level, (char *) log);
    }
}

void cci_log_remove(const char *path)
{
  logManager.removeLog(path);
}

void cci_log_set_level(Logger logger, CCI_LOG_LEVEL level)
{
  _Logger *l = (_Logger *) logger;

  if (l == NULL)
    {
      return;
    }

  l->setLevel(level);
}

bool cci_log_is_writable(Logger logger, CCI_LOG_LEVEL level)
{
  _Logger *l = (_Logger *) logger;

  if (l == NULL)
    {
      return false;
    }

  return l->isLoggerWritable(level);
}

void cci_log_set_force_flush(Logger logger, bool force_flush)
{
  _Logger *l = (_Logger *) logger;

  if (l == NULL)
    {
      return;
    }

  l->setForceFlush(force_flush);
}
