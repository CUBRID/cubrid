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
#include <time.h>
#else
#include <sys/time.h>
#endif

#include <iostream>
#include <fstream>
#include <string>
#include <map>

#include "cci_log.h"
#include "cci_common.h"

#define LOG_BUFFER_SIZE 16384
#define LOG_ER_OPEN -1

using namespace std;

namespace cci {
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

  int
  lock()
  {
    return cci_mutex_lock(&mutex);
  }

  int
  unlock()
  {
    return cci_mutex_unlock(&mutex);
  }
};
}

class _Logger
{
private:
  // members
  ofstream out;
  cci::_Mutex critical;
  string base;
  time_t roleTime;

  // methods
  void
  logPrefix(void)
  {
    struct timeval tv;
    struct tm cal;
    char buf[32];

    cci_gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &cal);
    cal.tm_year += 1900;
    cal.tm_mon += 1;

    snprintf(buf, 32, "<%d-%02d-%02d %02d:%02d:%02d.%03d> ", cal.tm_year,
        cal.tm_mon, cal.tm_mday, cal.tm_hour, cal.tm_min, cal.tm_sec,
        (int) (tv.tv_usec / 1000));

    out << buf;
  }

  int
  logInternal(char *msg)
  {
    critical.lock();
    logPrefix();
    out << msg << endl;
    critical.unlock();
    return 0;
  }

  bool
  isRole(int secs)
  {
    time_t role = roleTime / secs * secs;
    time_t now = time(0);

    now = now / secs * secs;
    return role != now;
  }

  string
  getDate(void)
  {
    struct tm cal;
    char buf[16];

    localtime_r(&roleTime, &cal);
    cal.tm_year += 1900;
    cal.tm_mon += 1;
    snprintf(buf, 16, "%d-%02d-%02d", cal.tm_year, cal.tm_mon, cal.tm_mday);
    return string(buf);
  }

  void
  open(void)
  {
    out.open(base.c_str(), fstream::out | fstream::app);
    if (out.fail())
      {
        throw LOG_ER_OPEN;
      }
  }

  void
  role(void)
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

  void
  hourRole()
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

  void
  dailyRole()
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

public:
  _Logger(const char *path)
  {
    base = string(path);
    roleTime = time(0);
    open();
  }

  int
  logInfo(char *msg)
  {
    return logInternal(msg);
  }

  void
  log(char *msg)
  {
    if (!out.is_open())
      {
        return;
      }

    dailyRole();
    logInternal(msg);
  }
};

typedef map<string, _Logger *> MapPathLogger;
typedef pair<string, _Logger *> PairPathLogger;
typedef MapPathLogger::iterator IteratorPathLogger;
static MapPathLogger mapPathLogger;

Logger
cci_log_add(const char *path)
{
  _Logger *logger = NULL;

  try
    {
      logger = new _Logger(path);
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
      string logPath(path);
      mapPathLogger.insert(PairPathLogger(logPath, logger));
    }

  return logger;
}

Logger
cci_log_get(const char *path)
{
  string logPath(path);
  IteratorPathLogger i = mapPathLogger.find(logPath);
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
  IteratorPathLogger i = mapPathLogger.begin();

  for (; i != mapPathLogger.end(); ++i)
    {
      delete i->second;
    }
  mapPathLogger.clear();
}

void
cci_log_write(Logger logger, const char *format, ...)
{
  _Logger *l = (_Logger *) logger;
  char buf[LOG_BUFFER_SIZE];
  va_list vl;

  va_start(vl, format);
  vsnprintf(buf, LOG_BUFFER_SIZE, format, vl);
  va_end(vl);

  l->log(buf);
}

