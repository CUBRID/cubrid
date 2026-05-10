/*
 *
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
 * system_metadata_upgrade.c
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "system_metadata_upgrade.h"
#include "crypt_opfunc.h"
#include "db.h"
#include "environment_variable.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "string_opfunc.h"
#include "upgrade_checksums.h"

#define UPGRADE_SCRIPT_FORMAT   "v%d_to_v%d.sql.enc"

static void sysmeta_upgrade_script_path (int from_v, int to_v, char *out, size_t outsize);
static int sysmeta_load_decoded_script (int from_v, int to_v, char **out_buf, size_t * out_len);

static void
sysmeta_upgrade_script_path (int from_v, int to_v, char *out, size_t outsize)
{
  char filename[NAME_MAX];

  snprintf (filename, sizeof (filename), UPGRADE_SCRIPT_FORMAT, from_v, to_v);
  envvar_upgradedir_file (out, outsize, filename);
}

static int
sysmeta_load_decoded_script (int from_v, int to_v, char **out_buf, size_t * out_len)
{
  char script_path[PATH_MAX];
  const char *expected_sha256 = UPGRADE_SCRIPT_SHA256S[from_v];
  struct stat st;
  FILE *fp = NULL;
  char *buf = NULL;
  long file_size = 0;
  SHA256_CTX sha_ctx;
  unsigned char digest[SHA256_DIGEST_LENGTH];
  char actual_sha256[SHA256_DIGEST_LENGTH * 2 + 1];
  int error = NO_ERROR;

  sysmeta_upgrade_script_path (from_v, to_v, script_path, sizeof (script_path));

  if (stat (script_path, &st) != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_UNKNOWN_FILE, 1, script_path);
      error = ER_FILE_UNKNOWN_FILE;
      goto exit;
    }
  file_size = st.st_size;

  fp = fopen (script_path, "rb");
  if (fp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_UNKNOWN_FILE, 1, script_path);
      error = ER_FILE_UNKNOWN_FILE;
      goto exit;
    }

  /* Migration scripts are bounded and one-shot; load whole file. */
  buf = (char *) malloc ((size_t) file_size + 1);
  if (buf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) file_size + 1);
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }

  if ((long) fread (buf, 1, (size_t) file_size, fp) != file_size)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_UNKNOWN_FILE, 1, script_path);
      error = ER_FILE_UNKNOWN_FILE;
      goto exit;
    }
  buf[file_size] = '\0';

  if (qstr_hex_to_bin (buf, (int) (file_size / 2), buf, (int) file_size) != (int) file_size)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SYSMETA_UPGRADE_TAMPERED, 0);
      error = ER_SYSMETA_UPGRADE_TAMPERED;
      goto exit;
    }

  SHA256_Init (&sha_ctx);
  SHA256_Update (&sha_ctx, buf, (size_t) file_size / 2);
  SHA256_Final (digest, &sha_ctx);
  str_to_hex_prealloced ((const char *) digest, SHA256_DIGEST_LENGTH,
			 actual_sha256, sizeof (actual_sha256), HEX_LOWERCASE);

  if (strcmp (actual_sha256, expected_sha256) != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SYSMETA_UPGRADE_TAMPERED, 0);
      error = ER_SYSMETA_UPGRADE_TAMPERED;
      goto exit;
    }

  *out_buf = buf;
  *out_len = (size_t) file_size / 2;
  buf = NULL;			/* ownership transferred — exit must not free */

exit:
  if (fp != NULL)
    {
      fclose (fp);
    }
  if (buf != NULL)
    {
      free_and_init (buf);
    }
  return error;
}

int
sysmeta_validate_upgrade_scripts (int from_version, int to_version)
{
  for (int v = from_version; v < to_version; v++)
    {
      char *buf = NULL;
      size_t len = 0;
      int error = sysmeta_load_decoded_script (v, v + 1, &buf, &len);
      if (error != NO_ERROR)
	{
	  return error;
	}
      free_and_init (buf);
    }
  return NO_ERROR;
}

int
sysmeta_print_upgrade_scripts (int from_version, int to_version)
{
  for (int v = from_version; v < to_version; v++)
    {
      char *buf = NULL;
      size_t len = 0;
      int error = sysmeta_load_decoded_script (v, v + 1, &buf, &len);
      if (error != NO_ERROR)
	{
	  return error;
	}
      fwrite (buf, 1, len, stdout);
      free_and_init (buf);
    }
  return NO_ERROR;
}

int
sysmeta_execute_sql_buffer (char *buf, size_t len)
{
  FILE *fp = NULL;
  DB_SESSION *session = NULL;
  int error = NO_ERROR;

  fp = fmemopen (buf, len, "r");
  if (fp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_UNKNOWN_FILE, 1, "(memory buffer)");
      error = ER_FILE_UNKNOWN_FILE;
      goto exit;
    }

  session = db_make_session_for_one_statement_execution (fp);
  if (session == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  while (true)
    {
      int stmt_cnt = db_parse_one_statement (session);
      if (stmt_cnt <= 0)
	{
	  break;
	}

      int stmt_id = db_compile_statement (session);
      if (stmt_id < 0)
	{
	  ASSERT_ERROR_AND_SET (error);
	  break;
	}
      if (stmt_id == 0)
	{
	  break;
	}

      DB_QUERY_RESULT *res = NULL;
      error = db_execute_statement (session, stmt_id, &res);
      if (res != NULL)
	{
	  db_query_end (res);
	}
      if (error < 0)
	{
	  break;
	}
      error = NO_ERROR;
    }

  if (error == NO_ERROR && db_get_errors (session) != NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	}
    }

exit:
  if (session != NULL)
    {
      db_close_session (session);
    }
  if (fp != NULL)
    {
      fclose (fp);
    }
  return error;
}

int
sysmeta_execute_upgrade_scripts (int from_version, int to_version)
{
  for (int v = from_version; v < to_version; v++)
    {
      char *buf = NULL;
      size_t len = 0;
      int error = sysmeta_load_decoded_script (v, v + 1, &buf, &len);
      if (error != NO_ERROR)
	{
	  return error;
	}

      error = sysmeta_execute_sql_buffer (buf, len);
      free_and_init (buf);

      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  return NO_ERROR;
}
