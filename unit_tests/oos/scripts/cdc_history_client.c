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


/* Verify historical values through the public CDC API. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cubrid_log.h"

int
main (int argc, char **argv)
{
  uint64_t lsa = 0;
  time_t stamp = time (NULL);
  int count = 0, seen[6][11] = { {0} }, idle = 0;
  int lifecycle = argc == 4 && strcmp (argv[1], "lifecycle") == 0;
  int triggers = getenv ("HISTORY_TRIGGER") != NULL && strcmp (getenv ("HISTORY_TRIGGER"), "1") == 0;
  int expected = (lifecycle ? 30 : 10) * (triggers ? 2 : 1);
  FILE *file;
  char expected_hex[65538];
  int entropy = getenv ("HISTORY_ENTROPY") != NULL && strcmp (getenv ("HISTORY_ENTROPY"), "1") == 0;
  if (entropy)
    {
      file = fopen ("/mnt/payload.hex", "r");
      if (file == NULL)
	return 2;
      if (fgets (expected_hex, sizeof (expected_hex), file) == NULL)
	{
	  fclose (file);
	  return 2;
	}
      fclose (file);
    }
  if (argc != 4)
    return 2;
  cubrid_log_set_tracelog ("/mnt/work", 3, 100);
  if (cubrid_log_set_all_in_cond (atoi (argv[3])) != 0
      || cubrid_log_set_extraction_timeout (5) != 0
      || cubrid_log_connect_server ("127.0.0.1", 1523, "history", "dba", "") != 0)
    return 3;
  if (strcmp (argv[1], "find") == 0)
    {
      if (cubrid_log_find_lsa (&stamp, &lsa) != 0)
	return 4;
      file = fopen (argv[2], "w");
      if (file == NULL)
	return 5;
      fprintf (file, "%llu\n", (unsigned long long) lsa);
      fclose (file);
      cubrid_log_finalize ();
      return 0;
    }
  file = fopen (argv[2], "r");
  if (file == NULL)
    return 5;
  {
    unsigned long long saved;
    if (fscanf (file, "%llu", &saved) != 1)
      {
	fclose (file);
	return 5;
      }
    lsa = saved;
  }
  fclose (file);
  if (strcmp (argv[1], "reject") == 0)
    {
      int attempt;
      for (attempt = 0; attempt < 2; attempt++)
	{
	  CUBRID_LOG_ITEM *items = NULL;
	  int size = 0, ret;
	  uint64_t previous = lsa;
	  ret = cubrid_log_extract (&lsa, &items, &size);
	  if (ret == CUBRID_LOG_SUCCESS)
	    {
	      CUBRID_LOG_ITEM *item;
	      for (item = items; item != NULL; item = item->next)
		if (item->data_item_type == 1)
		  return 10;
	      cubrid_log_clear_log_item (items);
	      attempt--;
	      continue;
	    }
	  if (ret != CUBRID_LOG_FAILED_EXTRACT || lsa != previous)
	    {
	      fprintf (stderr, "legacy rejection rc=%d size=%d lsa=%llu previous=%llu\n", ret, size,
		       (unsigned long long) lsa, (unsigned long long) previous);
	      return 10;
	    }
	}
      puts ("Unsupported history rejected twice without advancing the requested LSA");
      if (getenv ("HISTORY_REPOSITION") != NULL)
	{
	  int found = 0;
	  unsigned long long resume;
	  file = fopen ("resume.lsa", "r");
	  if (file == NULL)
	    return 11;
	  if (fscanf (file, "%llu", &resume) != 1)
	    {
	      fclose (file);
	      return 11;
	    }
	  fclose (file);
	  cubrid_log_finalize ();
	  if (cubrid_log_set_all_in_cond (atoi (argv[3])) != 0
	      || cubrid_log_connect_server ("127.0.0.1", 1523, "history", "dba", "") != 0)
	    return 11;
	  lsa = resume;
	  for (attempt = 0; attempt < 10 && !found; attempt++)
	    {
	      CUBRID_LOG_ITEM *items = NULL, *item;
	      int size = 0, ret = cubrid_log_extract (&lsa, &items, &size);
	      if (ret != CUBRID_LOG_SUCCESS)
		return 11;
	      for (item = items; item != NULL; item = item->next)
		if (item->data_item_type == 1)
		  {
		    DML *dml = &item->data_item.dml;
		    int id = 0, value = 0;
		    if (dml->dml_type != 0 || dml->num_changed_column != 2)
		      return 11;
		    memcpy (&id, dml->changed_column_data[0], sizeof (id));
		    memcpy (&value, dml->changed_column_data[1], sizeof (value));
		    if (id != 99 || value != 4633)
		      return 11;
		    found = 1;
		  }
	      cubrid_log_clear_log_item (items);
	    }
	  if (!found)
	    return 11;
	  puts ("Reconnected at supported history after checked rejection");
	}
      cubrid_log_finalize ();
      return 0;
    }
  while (idle < 20 && count < expected)
    {
      CUBRID_LOG_ITEM *items = NULL, *item;
      int size = 0, ret = cubrid_log_extract (&lsa, &items, &size);
      if (ret == CUBRID_LOG_SUCCESS)
	{
	  for (item = items; item != NULL; item = item->next)
	    {
	      DML *dml = &item->data_item.dml;
	      int kind = dml->dml_type % 3;
	      int id = 0, payload = 0, extra = 0, i;
	      if (item->data_item_type != 1 || (!lifecycle && kind != 2))
		continue;
	      if (dml->dml_type < 0 || dml->dml_type > (triggers ? 5 : 2))
		return 7;
	      int n = kind == 0 ? dml->num_changed_column : dml->num_cond_column;
	      int *indices = kind == 0 ? dml->changed_column_index : dml->cond_column_index;
	      char **values = kind == 0 ? dml->changed_column_data : dml->cond_column_data;
	      int *lengths = kind == 0 ? dml->changed_column_data_len : dml->cond_column_data_len;
	      for (i = 0; i < n; i++)
		{
		  if (indices[i] == 0)
		    memcpy (&id, values[i], sizeof (id));
		  if (indices[i] == 2)
		    {
		      char *value = values[i];
		      int j, len = lengths[i];
		      if (len != 1029 || value[0] != 'X' || value[1] != 39 || value[len - 1] != 39)
			return 6;
		      for (j = 2; j < len - 1; j++)
			if (value[j] != 'C' && value[j] != 'c')
			  return 6;
		      extra = 1;
		    }
		  if (indices[i] == 1)
		    {
		      char *value = values[i];
		      int j, len = lengths[i];
		      if (len != 65539 || value[0] != 'X' || value[1] != '\'' || value[len - 1] != '\'')
			{
			  fprintf (stderr, "bad payload length=%d prefix=%.20s\n", len, value);
			  return 6;
			}
		      for (j = 2; j < len - 1; j++)
			if (entropy && !(lifecycle && kind == 2))
			  {
			    if (value[j] != expected_hex[j - 2] && value[j] + 32 != expected_hex[j - 2])
			      return 6;
			  }
			else if (value[j] != (lifecycle && kind == 2 ? 'b' : 'a')
				 && value[j] != (lifecycle && kind == 2 ? 'B' : 'A'))
			  return 6;
		      payload = 1;
		    }
		}
	      if (kind == 1)
		{
		  int found = 0;
		  for (i = 0; i < dml->num_changed_column; i++)
		    if (dml->changed_column_index[i] == 1)
		      {
			char *value = dml->changed_column_data[i];
			int j, len = dml->changed_column_data_len[i];
			if (len != 65539 || value[0] != 'X' || value[1] != 39 || value[len - 1] != 39)
			  return 6;
			for (j = 2; j < len - 1; j++)
			  if (value[j] != 'B' && value[j] != 'b')
			    return 6;
			found = 1;
		      }
		  if (!found)
		    return 6;
		}
	      if (id < 1 || id > 10 || seen[dml->dml_type][id]
		  || ((atoi (argv[3]) || kind == 0) && (!payload || !extra)))
		return 7;
	      seen[dml->dml_type][id] = 1;
	      count++;
	    }
	  cubrid_log_clear_log_item (items);
	}
      else if (ret != CUBRID_LOG_SUCCESS_WITH_NO_LOGITEM && ret != CUBRID_LOG_SUCCESS_WITH_ADJUSTED_LSA
	       && ret != CUBRID_LOG_EXTRACTION_TIMEOUT)
	{
	  fprintf (stderr, "extraction failed: %d, rows=%d\n", ret, count);
	  return 8;
	}
      idle++;
    }
  cubrid_log_finalize ();
  printf ("Historical identities and bytes: %d/%d\n", count, expected);
  return count == expected ? 0 : 9;
}
