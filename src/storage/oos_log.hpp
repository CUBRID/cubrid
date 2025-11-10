/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * oos_log.hpp
 */

#pragma once

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <atomic>

enum class OOSLogLevel
{
  TRACE = 0,
  DEBUG,
  INFO,
  WARN,
  ERROR,
  FATAL,
};

// atomic runtime level
inline std::atomic<OOSLogLevel> oos_current_level{OOSLogLevel::INFO};

inline void oos_set_level (OOSLogLevel level)
{
  oos_current_level.store (level, std::memory_order_relaxed);
}

inline OOSLogLevel oos_get_level()
{
  return oos_current_level.load (std::memory_order_relaxed);
}

inline const char *oos_level_str (OOSLogLevel level)
{
  switch (level)
    {
    case OOSLogLevel::TRACE:
      return "TRACE";
    case OOSLogLevel::DEBUG:
      return "DEBUG";
    case OOSLogLevel::INFO:
      return "INFO";
    case OOSLogLevel::WARN:
      return "WARN";
    case OOSLogLevel::ERROR:
      return "ERROR";
    case OOSLogLevel::FATAL:
      return "FATAL";
    default:
      return "UNKNOWN";
    }
}

inline void oos_log_internal (OOSLogLevel level,
			      const char *file,
			      int line,
			      const char *func,
			      const char *fmt, ...)
{
  if (static_cast<int> (level) < static_cast<int> (oos_get_level()))
    {
      return;
    }

  std::time_t t = std::time (nullptr);
  std::tm tm{};
  localtime_r (&t, &tm);
  char timebuf[20];
  std::strftime (timebuf, sizeof (timebuf), "%H:%M:%S", &tm);

  std::fprintf (stderr, "[%s] OOS [%s](%s:%d): ",
		timebuf, oos_level_str (level), func, line);

  va_list args;
  va_start (args, fmt);
  std::vfprintf (stderr, fmt, args);
  va_end (args);
  std::fputc ('\n', stderr);
  std::fflush (stderr);
}


#if !defined (NDEBUG)
#define oos_trace(fmt, ...) \
    oos_log_internal(OOSLogLevel::TRACE, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define oos_debug(fmt, ...) \
    oos_log_internal(OOSLogLevel::DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define oos_info(fmt, ...) \
    oos_log_internal(OOSLogLevel::INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define oos_warn(fmt, ...) \
    oos_log_internal(OOSLogLevel::WARN, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define oos_error(fmt, ...) \
    oos_log_internal(OOSLogLevel::ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else

#define oos_trace(...)   do {} while (0)

#define oos_debug(...)   do {} while (0)

#define oos_info(...)    do {} while (0)

#define oos_warn(...)    do {} while (0)

#define oos_error(...)   do {} while (0)

#endif
