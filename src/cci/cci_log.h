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
 * cci_log.h -
 */

#ifndef CCI_LOG_H_
#define CCI_LOG_H_

typedef void *Logger;

typedef enum
{
  CCI_LOG_POSTFIX_NONE = 0,
  CCI_LOG_POSTFIX_DATE
} CCI_LOG_POSTFIX;

typedef enum
{
  CCI_LOG_LEVEL_OFF = 0,
  CCI_LOG_LEVEL_ERROR,
  CCI_LOG_LEVEL_WARN,
  CCI_LOG_LEVEL_INFO,
  CCI_LOG_LEVEL_DEBUG
} CCI_LOG_LEVEL;

#define CCI_LOG_ERROR(logger, log) \
  do { \
    if (cci_log_is_writable(logger, CCI_LOG_LEVEL_ERROR)) { \
      cci_log_write(CCI_LOG_LEVEL_ERROR, logger, log); \
    } \
  } while (false)

#define CCI_LOGF_ERROR(logger, ...) \
  do { \
    if (cci_log_is_writable(logger, CCI_LOG_LEVEL_ERROR)) { \
      cci_log_writef(CCI_LOG_LEVEL_ERROR, logger, __VA_ARGS__); \
    } \
  } while (false)

#define CCI_LOG_WARN(logger, log) \
  do { \
    if (cci_log_is_writable(logger, CCI_LOG_LEVEL_WARN)) { \
      cci_log_write(CCI_LOG_LEVEL_WARN, logger, log); \
    } \
  } while (false)

#define CCI_LOGF_WARN(logger, ...) \
  do { \
    if (cci_log_is_writable(logger, CCI_LOG_LEVEL_WARN)) { \
      cci_log_writef(CCI_LOG_LEVEL_WARN, logger, __VA_ARGS__); \
    } \
  } while (false)

#define CCI_LOG_INFO(logger, log) \
  do { \
    if (cci_log_is_writable(logger, CCI_LOG_LEVEL_INFO)) { \
      cci_log_write(CCI_LOG_LEVEL_INFO, logger, log); \
    } \
  } while (false)

#define CCI_LOGF_INFO(logger, ...) \
  do { \
    if (cci_log_is_writable(logger, CCI_LOG_LEVEL_INFO)) { \
      cci_log_writef(CCI_LOG_LEVEL_INFO, logger, __VA_ARGS__); \
    } \
  } while (false)

#define CCI_LOG_DEBUG(logger, log) \
  do { \
    if (cci_log_is_writable(logger, CCI_LOG_LEVEL_DEBUG)) { \
      cci_log_write(CCI_LOG_LEVEL_DEBUG, logger, log); \
    } \
  } while (false)

#define CCI_LOGF_DEBUG(logger, ...) \
  do { \
    if (cci_log_is_writable(logger, CCI_LOG_LEVEL_DEBUG)) { \
      cci_log_writef(CCI_LOG_LEVEL_DEBUG, logger, __VA_ARGS__); \
    } \
  } while (false)

#ifdef __cplusplus
extern "C"
{
#endif

  extern Logger cci_log_get (const char *path);
  extern void cci_log_finalize (void);
  extern void cci_log_writef (CCI_LOG_LEVEL level, Logger logger, const char *format, ...);
  extern void cci_log_write (CCI_LOG_LEVEL level, Logger logger, const char *log);
  extern void cci_log_remove (const char *path);
  extern void cci_log_set_level (Logger logger, CCI_LOG_LEVEL level);
  extern bool cci_log_is_writable (Logger logger, CCI_LOG_LEVEL level);
  extern void cci_log_set_force_flush (Logger logger, bool force_flush);
  extern void cci_log_use_default_newline (Logger logger, bool use_default_newline);
  extern void cci_log_use_default_prefix (Logger logger, bool use_default_prefix);
  extern void cci_log_change_max_file_size_appender (Logger logger, int max_file_size_kbytes, int max_backup_count);
  extern void cci_log_set_default_postfix (Logger logger, CCI_LOG_POSTFIX postfix);

#ifdef __cplusplus
}
#endif

#endif				/* CCI_LOG_H_ */
