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
 * test_oos_log.hpp
 */

#pragma once

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <atomic>

namespace test_oos_log
{
  enum class TestOosLogLevel
  {
    TRACE = 0,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL,
  };

// atomic runtime level
  inline std::atomic<TestOosLogLevel> test_oos_current_level{TestOosLogLevel::DEBUG};

  inline void test_oos_log_set_level (TestOosLogLevel level)
  {
    test_oos_current_level.store (level, std::memory_order_relaxed);
  }

  inline TestOosLogLevel test_oos_log_get_level()
  {
    return test_oos_current_level.load (std::memory_order_relaxed);
  }

  inline const char *test_oos_log_get_level_str (TestOosLogLevel level)
  {
    switch (level)
      {
      case TestOosLogLevel::TRACE:
	return "TRACE";
      case TestOosLogLevel::DEBUG:
	return "DEBUG";
      case TestOosLogLevel::INFO:
	return "INFO";
      case TestOosLogLevel::WARN:
	return "WARN";
      case TestOosLogLevel::ERROR:
	return "ERROR";
      case TestOosLogLevel::FATAL:
	return "FATAL";
      default:
	return "UNKNOWN";
      }
  }

  inline void test_oos_log_internal (TestOosLogLevel level,
				     const char *file,
				     int line,
				     const char *func,
				     const char *fmt, ...)
  {
    if (static_cast<int> (level) < static_cast<int> (test_oos_log_get_level()))
      {
	return;
      }

    std::time_t t = std::time (nullptr);
    std::tm tm{};
    localtime_r (&t, &tm);
    char timebuf[20];
    std::strftime (timebuf, sizeof (timebuf), "%H:%M:%S", &tm);

    std::fprintf (stderr, "[%s] TEST_OOS [%s](%s:%d): ",
		  timebuf, test_oos_log_get_level_str (level), func, line);

    va_list args;
    va_start (args, fmt);
    std::vfprintf (stderr, fmt, args);
    va_end (args);
    std::fputc ('\n', stderr);
    std::fflush (stderr);
  }


#if !defined (NDEBUG)
#define test_oos_trace(fmt, ...) \
    test_oos_log_internal(TestOosLogLevel::TRACE, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define test_oos_debug(fmt, ...) \
    test_oos_log_internal(TestOosLogLevel::DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define test_oos_info(fmt, ...) \
    test_oos_log_internal(TestOosLogLevel::INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define test_oos_warn(fmt, ...) \
    test_oos_log_internal(TestOosLogLevel::WARN, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define test_oos_error(fmt, ...) \
    test_oos_log_internal(TestOosLogLevel::ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else

#define test_oos_trace(...)   do {} while (0)

#define test_oos_debug(...)   do {} while (0)

#define test_oos_info(...)    do {} while (0)

#define test_oos_warn(...)    do {} while (0)

#define test_oos_error(...)   do {} while (0)

#endif

} // namespace test_oos_log

