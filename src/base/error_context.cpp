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
 * error_context.cpp - implementation for error context
 */

#include "error_context.hpp"

#include "error_code.h"
#include "error_manager.h"
#include "memory_alloc.h"
#if defined (SERVER_MODE)
#include "thread_entry.hpp"
#include "thread_manager.hpp"
#endif // SERVER_MODE

#include <cstring>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cuberr
{
  const bool LOG_ME = false; // todo: set false

  // logging macro's. macro's are used instead of inline functions for ARG_FILE_LINE
#define ERROR_CONTEXT_LOG(...) if (m_logging) _er_log_debug (ARG_FILE_LINE, __VA_ARGS__)

#define ERMSG_MSG "{ %p: errid=%d, sev=%d, fname=%s, line=%d, msg_area=%p(%s)_size=%zu, msgbuf=%p(%s), args=%p_n=%d }"
#define ERMSG_ARGS(em) &(em), (em).err_id, (em).severity, (em).file_name, (em).line_no, (em).msg_area, (em).msg_area, \
                       (em).msg_area_size, (em).msg_buffer, (em).msg_buffer, (em).args, (em).nargs
#define ERMSG_LOG(text, var) ERROR_CONTEXT_LOG (text #var " = " ERMSG_MSG, ERMSG_ARGS (var))

  thread_local context *tl_Context_p = NULL;

  er_message::er_message (const bool &logging)
    : err_id (NO_ERROR)
    , severity (ER_WARNING_SEVERITY)
    , file_name (NULL)
    , line_no (-1)
    , msg_area_size (sizeof (msg_buffer))
    , msg_area (msg_buffer)
    , args (NULL)
    , nargs (0)
    , msg_buffer {'\0'}
    , m_logging (logging)
  {
    //
  }

  er_message::~er_message ()
  {
    ERMSG_LOG ("destruct ", *this);
    clear_message_area ();
    clear_args ();
  }

  void er_message::swap (er_message &other)
  {
    ERMSG_LOG ("before_swap", *this);
    ERMSG_LOG ("before_swap", other);

    std::swap (this->err_id, other.err_id);
    std::swap (this->severity, other.severity);
    std::swap (this->file_name, other.file_name);
    std::swap (this->line_no, other.line_no);

    // msg_area
    const std::size_t bufsize = sizeof (this->msg_buffer);
    if (this->msg_area_size <= bufsize && other.msg_area_size <= bufsize)
      {
	assert (this->msg_area_size == bufsize && other.msg_area_size == bufsize);

	// swap buffer contexts
	char aux_buffer[bufsize];
	std::memcpy (aux_buffer, this->msg_buffer, bufsize);
	std::memcpy (this->msg_buffer, other.msg_buffer, bufsize);
	std::memcpy (other.msg_buffer, aux_buffer, bufsize);
      }
    else if (this->msg_area_size > bufsize && other.msg_area_size > bufsize)
      {
	/* swap pointers */
	std::swap (this->msg_area, other.msg_area);
	std::swap (this->msg_area_size, other.msg_area_size);
      }
    else if (this->msg_area_size <= bufsize)
      {
	assert (this->msg_area_size == bufsize);
	assert (other.msg_area_size > bufsize);

	// copy this buffer to other
	std::memcpy (other.msg_buffer, this->msg_buffer, bufsize);
	// move msg_area pointer from other to this
	this->msg_area = other.msg_area;
	other.msg_area = other.msg_buffer;
	// swap area size
	std::swap (this->msg_area_size, other.msg_area_size);
      }
    else
      {
	assert (this->msg_area_size > bufsize);
	assert (other.msg_area_size == bufsize);

	// copy other buffer to this
	std::memcpy (this->msg_buffer, other.msg_buffer, bufsize);
	// move msg_area pointer from this to other
	other.msg_area = this->msg_area;
	this->msg_area = this->msg_buffer;
	// swap area size
	std::swap (this->msg_area_size, other.msg_area_size);
      }

    assert ((this->msg_area_size == bufsize) == (this->msg_area == this->msg_buffer));
    assert ((other.msg_area_size == bufsize) == (other.msg_area == other.msg_buffer));
    assert (this->msg_area != other.msg_area);

    // swap args, nargs
    std::swap (this->args, other.args);
    std::swap (this->nargs, other.nargs);

    ERMSG_LOG ("after_swap", *this);
    ERMSG_LOG ("after_swap", other);
  }

  void
  er_message::clear_error ()
  {
    err_id = NO_ERROR;
    severity = ER_WARNING_SEVERITY;
    file_name = NULL;
    line_no = -1;
    msg_area[0] = '\0';
  }

  void
  er_message::set_error (int error_id_arg, int error_severity_arg, const char *filename_arg, int line_no_arg)
  {
    if (error_id_arg >= ER_FAILED || error_id_arg <= ER_LAST_ERROR)
      {
	assert (false);		/* invalid error id */
	error_id_arg = ER_FAILED;	/* invalid error id handling */
      }
    err_id = error_id_arg;
    severity = error_severity_arg;
    file_name = filename_arg;
    line_no = line_no_arg;
  }

  void
  er_message::clear_message_area (void)
  {
    if (msg_area != msg_buffer)
      {
	delete [] msg_area;

	msg_area = msg_buffer;
	msg_area_size = sizeof (msg_buffer);
      }
  }

  void
  er_message::clear_args (void)
  {
    if (args != NULL)
      {
	free_and_init (args);
      }
    nargs = 0;
  }

  void
  er_message::reserve_message_area (std::size_t size)
  {
    if (msg_area_size >= size)
      {
	// no need to resize
	return;
      }

    ERMSG_LOG ("before_resize", *this);

    std::size_t new_size = msg_area_size;
    while (new_size < size)
      {
	new_size *= 2;
      }
    clear_message_area ();

    msg_area = new char[new_size];
    msg_area_size = new_size;

    ERMSG_LOG ("after_resize", *this);
  }

  context::context (bool automatic_registration, bool logging)
    : m_base_level (m_logging)
    , m_stack ()
    , m_automatic_registration (automatic_registration)
    , m_logging (LOG_ME && logging)
    , m_destroyed (false)
  {
    if (automatic_registration)
      {
	register_thread_local ();
      }
  }

  context::~context (void)
  {
    // safe-guard: don't destroy twice
    assert (!m_destroyed);
    m_destroyed = true;

    if (tl_Context_p == this)
      {
	assert (m_automatic_registration);
	deregister_thread_local ();
      }
  }

  er_message &
  context::get_current_error_level (void)
  {
    if (m_stack.empty ())
      {
	return m_base_level;
      }
    else
      {
	return m_stack.top ();
      }
  }

  void
  context::register_thread_local (void)
  {
    assert (tl_Context_p == NULL);
    tl_Context_p = this;
  }

  void
  context::deregister_thread_local (void)
  {
#if defined (SERVER_MODE)
    // safe-guard that stacks are not "leaked"
    // ignore it for client (this is too late anyway)
    assert (m_stack.empty ());
#endif // SERVER_MODE

    clear_all_levels ();

    assert (tl_Context_p == this);
    tl_Context_p = NULL;
  }

  void
  context::clear_current_error_level (void)
  {
    get_current_error_level ().clear_error ();
  }

  void
  context::push_error_stack (void)
  {
    m_stack.emplace (m_logging);
  }

  void
  context::pop_error_stack (er_message &popped)
  {
    if (m_stack.empty ())
      {
	assert (false);
	return;
      }
    popped.swap (m_stack.top ());
    m_stack.pop ();
  }

  void
  context::pop_error_stack_and_destroy (void)
  {
    er_message temp (m_logging);
    pop_error_stack (temp);

    // popped memory is freed
  }

  bool
  context::has_error_stack (void)
  {
    return !m_stack.empty ();
  }

  const bool &
  context::get_logging (void)
  {
    return m_logging;
  }

  void
  context::clear_all_levels (void)
  {
    clear_stack ();
    m_base_level.clear_error ();
    m_base_level.clear_message_area ();
  }

  void
  context::clear_stack (void)
  {
    // clear stack by swapping; I hope this works
    std::stack<er_message> ().swap (m_stack);
  }

  cuberr::context &
  context::get_thread_local_context (void)
  {
    if (tl_Context_p == NULL)
      {
	assert (false);
	static context emergency_context (false, false);
#if defined (SERVER_MODE)
	if (cubthread::get_manager () != NULL)
	  {
	    return cubthread::get_entry ().get_error_context ();
	  }
#endif // SERVER_MODE
	return emergency_context;
      }
    return *tl_Context_p;
  }

  er_message &
  context::get_thread_local_error (void)
  {
    return get_thread_local_context ().get_current_error_level ();
  }


} // namespace cuberr
