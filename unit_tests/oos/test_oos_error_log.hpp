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
 * test_oos_error_log.hpp - reading the server error log from an OOS test
 *
 * A diagnostic that is only written to the server error log is observable nowhere else, so a test that
 * asserts one arrived (or stayed away) has to read the log. Take an offset before the operation and ask
 * what the log gained after it; the whole file also holds unrelated lines from earlier tests.
 */

#ifndef _TEST_OOS_ERROR_LOG_HPP_
#define _TEST_OOS_ERROR_LOG_HPP_

#include <cctype>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "error_manager.h"
#include "system_parameter.h"

namespace test_oos_error_log
{

  /* The file er_start actually opened. er_get_msglog_filename returns the configured base name, but the
   * error manager appends the process id to it when er_production_mode is off at the moment it opens the
   * file (see er_start in src/base/error_manager.c) - and a unit test calls er_init before the
   * configuration is loaded, so the parameter's value later is not proof of which name was used. Pick
   * whichever of the two exists; er_start opened exactly one. */
  inline std::string error_log_path ()
  {
    const char *name = er_get_msglog_filename ();
    if (name == NULL)
      {
	return {};
      }
    struct stat st;
    const std::string with_pid = std::string (name) + "." + std::to_string ((long) getpid ());
    if (stat (with_pid.c_str (), &st) == 0)
      {
	return with_pid;
      }
    return std::string (name);
  }

  /* Current size of the server error log, or -1 when it is not a readable file. Take this BEFORE the
   * operation under test and pass it to the queries below. */
  inline long error_log_size ()
  {
    const std::string path = error_log_path ();
    struct stat st;
    if (path.empty () || stat (path.c_str (), &st) != 0)
      {
	return -1;
      }
    return (long) st.st_size;
  }

  /* Everything the error log gained after the given offset; empty when it is not a readable file. */
  inline std::string error_log_tail (long offset)
  {
    const std::string path = error_log_path ();
    if (path.empty () || offset < 0)
      {
	return {};
      }
    FILE *fp = std::fopen (path.c_str (), "rb");
    if (fp == NULL)
      {
	return {};
      }
    std::fseek (fp, offset, SEEK_SET);
    std::string tail;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread (buf, 1, sizeof (buf), fp)) > 0)
      {
	tail.append (buf, n);
      }
    std::fclose (fp);
    return tail;
  }

  /* True iff the text holds an error-log line for error_code. The character after the number differs by
   * build (a standalone log writes "CODE = -1385 Tran", a server log "CODE = -1385, Tran"), so the match
   * requires only that a digit does not follow: without that, -1385 would also match a hypothetical
   * -13850. */
  inline bool text_mentions_error (const std::string &tail, int error_code)
  {
    const std::string needle = "CODE = " + std::to_string (error_code);
    for (std::size_t at = tail.find (needle); at != std::string::npos; at = tail.find (needle, at + 1))
      {
	const std::size_t after = at + needle.size ();
	if (after >= tail.size () || std::isdigit (static_cast<unsigned char> (tail[after])) == 0)
	  {
	    return true;
	  }
      }
    return false;
  }

  /* True iff the error log gained a line for error_code after the given offset. */
  inline bool error_log_mentions_since (long offset, int error_code)
  {
    return text_mentions_error (error_log_tail (offset), error_code);
  }

  /* True iff the error log gained the given text after the given offset. */
  inline bool error_log_contains_since (long offset, const std::string &text)
  {
    return error_log_tail (offset).find (text) != std::string::npos;
  }

  /* Admits notifications to the server error log for the scope's lifetime, so that a test asserting the
   * ABSENCE of a notification cannot pass merely because the loaded configuration filtered notifications
   * out, and one asserting its presence cannot fail for that reason. */
  struct notification_log_scope
  {
    int saved_level;

    notification_log_scope ()
      : saved_level (prm_get_integer_value (PRM_ID_ER_LOG_LEVEL))
    {
      prm_set_integer_value (PRM_ID_ER_LOG_LEVEL, ER_NOTIFICATION_SEVERITY);
    }

    ~notification_log_scope ()
    {
      prm_set_integer_value (PRM_ID_ER_LOG_LEVEL, saved_level);
    }
  };

} // namespace test_oos_error_log

#endif /* _TEST_OOS_ERROR_LOG_HPP_ */
