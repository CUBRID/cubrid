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
#else
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <iostream>
#include <fstream>
#include <string>
#include <map>

#include "cci_log.h"
#include "cci_common.h"

static const int LOG_BUFFER_SIZE = 16384;
static const int LOG_ER_OPEN = -1;
static const long int LOG_FLUSH_SIZE = 1024 * 1024;	/* byte */
static const long int LOG_FLUSH_USEC = 1 * 1000000;	/* usec */

using namespace std;

namespace cci
{
  class _Mutex
  {
  private:
    cci_mutex_t mutex;

  public:
    _Mutex()
    {
      cci_mutex_init(&mutex, NULL);
    }

    ~_Mutex()
    {
      cci_mutex_destroy(&mutex);
    }

    int lock()
    {
      return cci_mutex_lock(&mutex);
    }

    int unlock()
    {
      return cci_mutex_unlock(&mutex);
    }
  };
}

class _Logger
{
public:
  _Logger(const char *path):base(path), roleTime(time(0)),
    level(CCI_LOG_LEVEL_INFO), unflushedBytes(0), nextFlushTime(0)
  {
  }

  virtual ~ _Logger()
  {
    flush();
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

private:
  void write(const char *msg)
  {
    out << msg;

    unflushedBytes += strlen(msg);

    if (unflushedBytes >= LOG_FLUSH_SIZE || now() >= nextFlushTime)
      {
        flush();
      }
  }

  void logPrefix(CCI_LOG_LEVEL level)
  {
    struct timeval tv;
    struct tm cal;

    cci_gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &cal);
    cal.tm_year += 1900;
    cal.tm_mon += 1;

    char buf[128];
    unsigned long tid = getThreadId();
    snprintf(buf, 128,
        "%d-%02d-%02d %02d:%02d:%02d.%03d [TID:%lu] [%5s]", cal.tm_year,
        cal.tm_mon, cal.tm_mday, cal.tm_hour, cal.tm_min, cal.tm_sec,
        (int)(tv.tv_usec / 1000), tid, cciLogLevelStr[level]);

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
            makeDirectory(dir);
          }
        q++;
      }
  }

  long int now()
  {
    struct timeval tv;
    cci_gettimeofday(&tv, NULL);
    return tv.tv_usec;
  }

protected:
  virtual void makeDirectory(const char *path) = 0;
  virtual unsigned int getThreadId() = 0;

private:
  ofstream out;
  cci::_Mutex critical;
  string base;
  time_t roleTime;
  CCI_LOG_LEVEL level;
  long int unflushedBytes;
  long int nextFlushTime;
};

#ifdef WINDOWS
class WindowsLogger:public _Logger
{
public:
  WindowsLogger(const char *path):_Logger(path)
  {
  }

protected:
  virtual void makeDirectory(const char *path)
  {
    CreateDirectory(path, NULL);
  }

  virtual unsigned int getThreadId()
  {
    return GetCurrentThreadId();
  }
};
#else
class LinuxLogger:public _Logger
{
public:
  LinuxLogger(const char *path):_Logger(path)
  {
  }

protected:
  virtual void makeDirectory(const char *path)
  {
    mkdir(path, 0755);
  }

  virtual unsigned int getThreadId()
  {
    return getpid();
  }
};
#endif

typedef map < string, _Logger * >MapPathLogger;
typedef
MapPathLogger::iterator
IteratorPathLogger;
static
MapPathLogger
mapPathLogger;

Logger
cci_log_add(const char *path)
{
  _Logger *
  logger = NULL;

  try
    {
#ifdef WINDOWS
      logger = new WindowsLogger(path);
#else
      logger = new LinuxLogger(path);
#endif
      logger->open();
    }
  catch (...)
    {
      if (logger != NULL)
        {
          delete
          logger;
        }
      return NULL;
    }

  if (logger != NULL)
    {
      mapPathLogger[path] = logger;
    }

  return logger;
}

Logger
cci_log_get(const char *path)
{
  IteratorPathLogger
  i = mapPathLogger.find(path);
  if (i != mapPathLogger.end())
    {
      return i->second;
    }
  else
    {
      return cci_log_add(path);
    }
}

void
cci_log_finalize(void)
{
  IteratorPathLogger
  i = mapPathLogger.begin();

  for (; i != mapPathLogger.end(); ++i)
    {
      delete
      i->
      second;
    }
  mapPathLogger.clear();
}

void
cci_log_write(CCI_LOG_LEVEL level, Logger logger, const char *format, ...)
{
  _Logger *
  l = (_Logger *) logger;

  if (l == NULL)
    {
      return;
    }

  if (l->isLoggerWritable(level))
    {
      char
      buf[LOG_BUFFER_SIZE];
      va_list
      vl;

      va_start(vl, format);
      vsnprintf(buf, LOG_BUFFER_SIZE, format, vl);
      va_end(vl);

      l->log(level, buf);
    }
}

void
cci_log_remove(const char *path)
{
  IteratorPathLogger
  i = mapPathLogger.find(path);
  if (i != mapPathLogger.end())
    {
      delete
      i->
      second;
      mapPathLogger.erase(i);
    }
}

void
cci_log_set_level(Logger logger, CCI_LOG_LEVEL level)
{
  _Logger *
  l = (_Logger *) logger;

  if (l == NULL)
    {
      return;
    }

  l->setLevel(level);
}
