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
 * release_string.h - release related information (at client and server)
 */

#ifndef _RELEASE_STRING_H_
#define _RELEASE_STRING_H_

#ident "$Id$"

#include "config.h"

#define REL_MAX_RELEASE_LENGTH 15
#define REL_MAX_VERSION_LENGTH 256

/*
 * REL_FIXUP_FUNCTION - Signature for a function that can part of
 *                      a disk compatibility rule.
 *                      An array of these functions can be returned by
 *                      rel_get_disk_compatible.
 */
typedef void (*REL_FIXUP_FUNCTION) (void);

/*
 * REL_COMPATIBILITY - Describes the various types of compatibility we can have.
 *                     Returned by the rel_get_disk_compatible function.
 */
typedef enum
{
  REL_NOT_COMPATIBLE,
  REL_FULLY_COMPATIBLE,
  REL_FORWARD_COMPATIBLE,
  REL_BACKWARD_COMPATIBLE
} REL_COMPATIBILITY;

extern const char *rel_name (void);
extern const char *rel_release_string (void);
extern const char *rel_major_release_string (void);
extern const char *rel_build_number (void);
extern const char *rel_build_os (void);
extern const char *rel_build_type (void);

#if defined(VERSION_STRING)
extern const char *rel_version_string (void);
#endif /* VERSION_STRING */
#if defined(ENABLE_UNUSED_FUNCTION)
extern const char *rel_copyright_header (void);
extern const char *rel_copyright_body (void);
#endif
extern float rel_disk_compatible (void);
extern void rel_set_disk_compatible (float level);
extern int rel_bit_platform (void);

extern int rel_compare (const char *rel_a, const char *rel_b);
extern REL_COMPATIBILITY rel_get_disk_compatible (float db_level, REL_FIXUP_FUNCTION ** fixups);
extern bool rel_is_log_compatible (const char *writer_rel_str, const char *reader_rel_str);
extern REL_COMPATIBILITY rel_get_net_compatible (const char *client_rel_str, const char *server_rel_str);
extern void rel_copy_version_string (char *buf, size_t len);
#endif /* _RELEASE_STRING_H_ */
