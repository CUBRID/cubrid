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
 * vector_float.cpp
 */

#include "vector_float.hpp"

int
or_put_float_array_internal (OR_BUF *buf, const float *float_array, int array_length, int align)
{
  int net_length;
  char *start;
  int status;
  int valid_buf;
  jmp_buf save_buf;

  /* Save the error environment if it exists */
  if (buf->error_abort)
    {
      memcpy (&save_buf, &buf->env, sizeof (save_buf));
    }
  valid_buf = buf->error_abort;
  buf->error_abort = 1;

  /* Set up error jump point */
  status = _setjmp (buf->env);

  if (status == 0)
    {
      start = buf->ptr;

      /* Store the array length */
      if (array_length < 0xFF)
	{
	  or_put_byte (buf, array_length);
	}
      else
	{
	  or_put_byte (buf, 0xFF);
	  OR_PUT_INT (&net_length, array_length);
	  or_put_data (buf, (char *)&net_length, OR_INT_SIZE);
	}

      /* Store the float array */
      or_put_data (buf, (char *)float_array, array_length * sizeof (float));

      /* Align to word boundary if requested */
      if (align == INT_ALIGNMENT)
	{
	  or_put_align32 (buf);
	}
    }
  else
    {
      /* Error handling - restore environment if valid */
      if (valid_buf)
	{
	  memcpy (&buf->env, &save_buf, sizeof (save_buf));
	  _longjmp (buf->env, status);
	}
    }

  /* Restore the error handling state */
  if (valid_buf)
    {
      memcpy (&buf->env, &save_buf, sizeof (save_buf));
    }
  else
    {
      buf->error_abort = 0;
    }

  /* Return appropriate status */
  if (status == 0)
    {
      return NO_ERROR;
    }

  return status;
}

