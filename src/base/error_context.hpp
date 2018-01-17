/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * error_context.hpp - interface for error context
 */

#ifndef _ERROR_CONTEXT_HPP_
#define _ERROR_CONTEXT_HPP_

const size_t ER_EMERGENCY_BUF_SIZE = 256;

// legacy structures
typedef union er_va_arg ER_VA_ARG;
union er_va_arg
{
  int int_value;		/* holders for the values that we actually */
  void *pointer_value;		/* retrieve from the va_list. */
  double double_value;
  long double longdouble_value;
  const char *string_value;
  long long longlong_value;
};

typedef struct er_messages ER_MSG;
struct er_messages
{
  int err_id;			/* Error identifier of the current message */
  int severity;			/* Warning, Error, FATAL Error, etc... */
  const char *file_name;	/* File where the error is set */
  int line_no;			/* Line in the file where the error is set */
  int msg_area_size;		/* Size of the message area */
  char *msg_area;		/* Pointer to message area */
  ER_MSG *stack;		/* Stack to previous error messages */
  ER_VA_ARG *args;		/* Array of va_list entries */
  int nargs;			/* Length of array */
  er_messages ();
  ~er_messages ();

  ER_MSG *pop_stack (void);
};

namespace cuberr
{
  class context
  {
    public:
      context ();

      ~context () = default;

      const ER_MSG &get_crt_error (void);

      static context &get_context (void);

    private:
      ER_MSG m_all_errors;
      ER_MSG &m_crt_error_p;
      char m_msgbuf[ER_EMERGENCY_BUF_SIZE];
  };
} // namespace cuberr

#endif // _ERROR_CONTEXT_HPP_s
