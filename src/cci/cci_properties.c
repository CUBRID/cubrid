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
 * cci_properties.c -
 */

#ident "$Id$"

/*
 * IMPORTED SYSTEM HEADER FILES
 */
#include "config.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>

/*
 * OTHER IMPORTED HEADER FILES
 */
#include "cci_handle_mng.h"
#include "cci_log.h"

/*
 * PRIVATE DEFINITIONS
 */
typedef enum
{
  BOOL_PROPERTY,
  INT_PROPERTY,
  STRING_PROPERTY
} T_TYPE_PROPERTY;

typedef struct
{
  const char *name;
  T_TYPE_PROPERTY type;
  void *data;
} T_URL_PROPERTY;

/*
 * PRIVATE FUNCTION PROTOTYPES
 */
static int cci_url_set_althosts (T_CON_HANDLE * handle, char *data);
static int cci_url_parse_properties (T_URL_PROPERTY props[], int len,
				     char *properties);
static int cci_url_set_properties (T_URL_PROPERTY props[], int len,
				   char *name, char *value);
static int cci_url_set_value (T_URL_PROPERTY * property, char *value);
static int cci_url_get_int (char *str, int *value);
static int cci_url_get_bool (char *str, bool * value);

/*
 * INTERFACE VARIABLES
 */

/*
 * PUBLIC VARIABLES
 */

/*
 * PRIVATE VARIABLES
 */

/*
 * IMPLEMENTATION OF INTERFACE FUNCTIONS
 */
static int
cci_url_get_bool (char *str, bool * value)
{
  static const char *accepts[] = {
    "true", "false", "on", "off", "yes", "no"
  };
  int i, dim;

  if (value == NULL)
    {
      return CCI_ER_INVALID_URL;
    }

  dim = DIM (accepts);
  for (i = 0; i < dim; i++)
    {
      if (strcasecmp (accepts[i], str) == 0)
	{
	  *value = (i % 2) == 0;
	  return CCI_ER_NO_ERROR;
	}
    }

  return CCI_ER_INVALID_URL;
}

static int
cci_url_get_int (char *str, int *value)
{
  int v;
  char *end;

  if (value == NULL)
    {
      return CCI_ER_INVALID_URL;
    }

  v = strtol (str, &end, 10);
  if (end != NULL && end[0] != '\0')
    {
      return CCI_ER_INVALID_URL;
    }

  *value = v;
  return CCI_ER_NO_ERROR;
}

static int
cci_url_set_value (T_URL_PROPERTY * property, char *value)
{
  int error;

  switch (property->type)
    {
    case BOOL_PROPERTY:
      {
	bool v;
	error = cci_url_get_bool (value, &v);
	if (error == CCI_ER_NO_ERROR)
	  {
	    property->data = (void *) v;
	  }
	break;
      }
    case INT_PROPERTY:
      {
	int v;
	error = cci_url_get_int (value, &v);
	if (error == CCI_ER_NO_ERROR)
	  {
	    property->data = (void *) v;
	  }
	break;
      }
    case STRING_PROPERTY:
      {
	property->data = value;
	break;
      }
    default:
      return CCI_ER_INVALID_URL;
    }

  return error;
}

static int
cci_url_set_properties (T_URL_PROPERTY props[], int len, char *name,
			char *value)
{
  int i, error = CCI_ER_NO_ERROR;

  if (name == NULL || value == NULL)
    {
      return CCI_ER_INVALID_URL;
    }

  for (i = 0; i < len && error == CCI_ER_NO_ERROR; i++)
    {
      if (strcasecmp (name, props[i].name) == 0)
	{
	  error = cci_url_set_value (&props[i], value);
	  return error;
	}
    }

  if (i == len)
    {
      return CCI_ER_INVALID_URL;
    }

  return error;
}

static int
cci_url_parse_properties (T_URL_PROPERTY props[], int len, char *properties)
{
  char *token, *save_url = NULL;
  int error = CCI_ER_NO_ERROR;

  if (props == NULL)
    {
      return CCI_ER_INVALID_URL;
    }

  token = strtok_r (properties, "&", &save_url);
  while (token != NULL && error == CCI_ER_NO_ERROR)
    {
      char *name, *value, *save_property = NULL;
      name = strtok_r (token, "=", &save_property);
      value = strtok_r (NULL, "=", &save_property);
      error = cci_url_set_properties (props, len, name, value);

      token = strtok_r (NULL, "&", &save_url);
    }

  return error;
}

static int
cci_url_set_althosts (T_CON_HANDLE * handle, char *data)
{
  T_ALTER_HOST *hosts = handle->alter_hosts;
  char *token, *save_data = NULL, *end;
  int i, error = CCI_ER_NO_ERROR;

  token = strtok_r (data, ",", &save_data);
  for (i = 0; token != NULL; i++)
    {
      char *host, *port, *save_alter = NULL;
      int v;

      if (i >= ALTER_HOST_MAX_SIZE)
	{
	  return CCI_ER_INVALID_URL;
	}

      host = strtok_r (token, ":", &save_alter);
      error = hm_ip_str_to_addr (host, hosts[i].ip_addr);
      if (error < 0)
	{
	  return error;
	}

      port = strtok_r (NULL, ":", &save_alter);
      v = strtol (port, &end, 10);
      if (v <= 0 || (end != NULL && end[0] != '\0'))
	{
	  return CCI_ER_INVALID_URL;
	}
      hosts[i].port = v;
    }
  handle->alter_host_count = i;

  return error;
}

int
cci_conn_set_properties (T_CON_HANDLE * handle, char *properties)
{
  T_URL_PROPERTY props[] = {
    {"altHosts", STRING_PROPERTY, NULL},
    {"rcTime", INT_PROPERTY, (void *) 600},
    {"autoCommit", BOOL_PROPERTY, (void *) true},
    {"loginTimeout", INT_PROPERTY, (void *) 0},
    {"queryTimeout", INT_PROPERTY, (void *) 0},
    {"disconnectOnQueryTimeout", BOOL_PROPERTY, (void *) false},
    {"logFile", STRING_PROPERTY, NULL},
    {"logOnException", BOOL_PROPERTY, (void *) false},
    {"logSlowQueries", BOOL_PROPERTY, (void *) false},
    {"slowQueryThresholdMillis", INT_PROPERTY, (void *) 60000},
    {"logTraceApi", BOOL_PROPERTY, (void *) false},
    {"logTraceNetwork", BOOL_PROPERTY, (void *) false},
    {"logBaseDir", STRING_PROPERTY, (void *) "logs"}
  };
  int error = CCI_ER_NO_ERROR;

  error = cci_url_parse_properties (props, DIM (props), properties);
  if (error != CCI_ER_NO_ERROR)
    {
      goto set_properties_end;
    }

  error = cci_url_set_althosts (handle, props[0].data);
  if (error != CCI_ER_NO_ERROR)
    {
      goto set_properties_end;
    }
  handle->rc_time = (int) props[1].data;
  handle->autocommit_mode = (bool) props[2].data;
  handle->login_timeout = (int) props[3].data;
  handle->query_timeout = (int) props[4].data;
  handle->disconnect_on_query_timeout = (bool) props[5].data;

  /* for logging */
  if (handle->log_filename)
    {
      handle->log_filename = strdup (props[6].data);
    }
  else
    {
      char file[PATH_MAX];
      snprintf (file, PATH_MAX, "%s/cci_%04d.log", (char *) props[12].data,
		handle->id);
      mkdir (props[12].data, 0755);
      handle->log_filename = strdup (file);
    }
  if (handle->log_filename == NULL)
    {
      error = CCI_ER_NO_MORE_MEMORY;
      goto set_properties_end;
    }
  handle->log_on_exception = (bool) props[7].data;
  handle->log_slow_queries = (bool) props[8].data;
  handle->slow_query_threshold_millis = (int) props[9].data;
  handle->log_trace_api = (bool) props[10].data;
  handle->log_trace_network = (bool) props[11].data;

  if (handle->log_on_exception || handle->log_slow_queries
      || handle->log_trace_api || handle->log_trace_network)
    {
      handle->logger = cci_log_get (handle->log_filename);
    }

set_properties_end:
  API_SLOG (handle);
  API_ELOG (handle, error);
  return error;
}
