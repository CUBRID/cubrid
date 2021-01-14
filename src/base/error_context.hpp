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
 * error_context.hpp - interface for error context
 */

#ifndef _ERROR_CONTEXT_HPP_
#define _ERROR_CONTEXT_HPP_

#include <cstddef>

#include <stack>

namespace cuberr
{
  const std::size_t ER_EMERGENCY_BUF_SIZE = 256;

  // legacy structures
  union er_va_arg
  {
    int int_value;		/* holders for the values that we actually */
    void *pointer_value;		/* retrieve from the va_list. */
    double double_value;
    long double longdouble_value;
    const char *string_value;
    long long longlong_value;
  };

  struct er_message
  {
    public:
      er_message (const bool &logging);
      ~er_message ();

      void swap (er_message &other);

      void clear_error (void);            // clear error and message
      void set_error (int error_id, int error_severity, const char *filename, int line_no);  // set error
      void reserve_message_area (std::size_t size);
      void clear_message_area (void);
      void clear_args (void);

      int err_id;			/* Error identifier of the current message */
      int severity;			/* Warning, Error, FATAL Error, etc... */
      const char *file_name;	/* File where the error is set */
      int line_no;			/* Line in the file where the error is set */
      std::size_t msg_area_size;		/* Size of the message area */
      char *msg_area;		/* Pointer to message area */
      er_va_arg *args;		/* Array of va_list entries */
      int nargs;			/* Length of array */
      char msg_buffer[ER_EMERGENCY_BUF_SIZE];   // message buffer

    private:
      // not copy constructible
      er_message (er_message &);

      const bool &m_logging; // reference to context logging
  };

  class context
  {
    public:

      // constructor
      // automatic_registration - if true, registration/deregistration is handled during construct/destruct
      // logging - if true, extensive logging is activated.
      //           NOTE: "nested" logging is possible and er_Log_file_mutex may be locked twice causing a hang.
      //                 fix this if you want to use the error context logging.
      //
      context (bool automatic_registration = false, bool logging = false);

      ~context ();

      er_message &get_current_error_level (void);

      void register_thread_local (void);
      void deregister_thread_local (void);

      void clear_current_error_level (void);
      void push_error_stack (void);
      void pop_error_stack (er_message &popped);  // caller will destroy popped
      void pop_error_stack_and_destroy (void);
      bool has_error_stack (void);

      const bool &get_logging (void);

      static context &get_thread_local_context (void);
      static er_message &get_thread_local_error (void);

    private:
      void clear_all_levels (void);
      void clear_stack (void);

      er_message m_base_level;
      std::stack<er_message> m_stack;
      bool m_automatic_registration;
      bool m_logging;                               // activate logging
      bool m_destroyed;                             // set to true on destruction
  };
} // namespace cuberr

#endif // _ERROR_CONTEXT_HPP_
