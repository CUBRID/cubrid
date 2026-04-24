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

#ifndef _OOS_LOG_HPP_
#define _OOS_LOG_HPP_

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <atomic>
#include <climits>

#include "environment_variable.h"

namespace oos_log
{
  enum class OosLogLevel
  {
    TRACE = 0,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL,
  };

// atomic runtime level
  inline std::atomic<OosLogLevel> oos_current_level{OosLogLevel::DEBUG};

  inline void oos_log_set_level (OosLogLevel level)
  {
    oos_current_level.store (level, std::memory_order_relaxed);
  }

  inline OosLogLevel oos_log_get_level()
  {
    return oos_current_level.load (std::memory_order_relaxed);
  }

  inline const char *oos_log_get_level_str (OosLogLevel level)
  {
    switch (level)
      {
      case OosLogLevel::TRACE:
	return "TRACE";
      case OosLogLevel::DEBUG:
	return "DEBUG";
      case OosLogLevel::INFO:
	return "INFO";
      case OosLogLevel::WARN:
	return "WARN";
      case OosLogLevel::ERROR:
	return "ERROR";
      case OosLogLevel::FATAL:
	return "FATAL";
      default:
	return "UNKNOWN";
      }
  }

  // C++11 magic static: the standard guarantees that initialization of a
  // function-local static variable is thread-safe (§6.7/4).  If multiple
  // threads enter concurrently, exactly one performs the initializer while
  // the others block until it completes — no explicit mutex or atomic needed.
  inline FILE *oos_log_get_file()
  {
    static FILE *s_fp = []() -> FILE *
    {
      char path[PATH_MAX];
      envvar_logdir_file (path, sizeof (path), "oos.log");
      return std::fopen (path, "a");
    }();
    return s_fp;
  }

  inline void oos_log_internal (OosLogLevel level,
				const char *file,
				int line,
				const char *func,
				const char *fmt, ...)
  {
    if (static_cast<int> (level) < static_cast<int> (oos_log_get_level()))
      {
	return;
      }

    std::time_t t = std::time (nullptr);
    std::tm tm{};
    localtime_r (&t, &tm);
    char timebuf[20];
    std::strftime (timebuf, sizeof (timebuf), "%H:%M:%S", &tm);

    char header[512];
    std::snprintf (header, sizeof (header), "[%s] OOS [%s](%s:%d): ",
		   timebuf, oos_log_get_level_str (level), func, line);

    char body[2048];
    va_list args;
    va_start (args, fmt);
    std::vsnprintf (body, sizeof (body), fmt, args);
    va_end (args);

    // file: $CUBRID/log/oos.log is always the primary sink.  Writing to an
    // inherited stderr tty corrupts the controlling terminal (cgdb UI, or a
    // foreground csql/readline session that has switched the tty to raw mode),
    // so stderr is opt-in via the CUBRID_OOS_LOG_STDERR environment variable
    // (intended for local development, not for daemonized cub_server).
    FILE *logfp = oos_log_get_file();
    bool file_written = false;
    if (logfp != nullptr)
      {
	std::fputs (header, logfp);
	std::fputs (body, logfp);
	std::fputc ('\n', logfp);
	std::fflush (logfp);
	file_written = true;
      }

    // Presence-only check: any value including "0"/"false"/"" enables stderr.
    // To disable, the variable must be unset.  This matches the documented
    // opt-in contract (PR description) and avoids bike-shedding over truthy
    // string parsing.
    static const bool stderr_enabled = (std::getenv ("CUBRID_OOS_LOG_STDERR") != nullptr);

    // Fallback: if the file sink is unavailable (e.g. $CUBRID unset, log dir
    // unwritable, fopen failed) AND the message is ERROR/WARN, force stderr
    // so that release-build diagnostics are never silently dropped — this
    // preserves the contract documented at the bottom of this header
    // ("oos_error and oos_warn are always active").
    const bool force_stderr_fallback =
	    !file_written && static_cast<int> (level) >= static_cast<int> (OosLogLevel::WARN);

    if (stderr_enabled || force_stderr_fallback)
      {
	std::fputs (header, stderr);
	std::fputs (body, stderr);
	std::fputc ('\n', stderr);
	std::fflush (stderr);
      }
  }

} // namespace oos_log


/* oos_error and oos_warn are always active — they must be visible in release builds
 * for replication and QA diagnostics.  Lower levels are debug-only. */
#define oos_error(fmt, ...) \
    oos_log::oos_log_internal(oos_log::OosLogLevel::ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define oos_warn(fmt, ...) \
    oos_log::oos_log_internal(oos_log::OosLogLevel::WARN, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#if !defined (NDEBUG)
#define oos_trace(fmt, ...) \
    oos_log::oos_log_internal(oos_log::OosLogLevel::TRACE, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define oos_debug(fmt, ...) \
    oos_log::oos_log_internal(oos_log::OosLogLevel::DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define oos_info(fmt, ...) \
    oos_log::oos_log_internal(oos_log::OosLogLevel::INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else

#define oos_trace(...)   do {} while (0)

#define oos_debug(...)   do {} while (0)

#define oos_info(...)    do {} while (0)

#endif

#endif /* _OOS_LOG_HPP_ */
