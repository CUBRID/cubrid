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
 * load_common.cpp - common code used by loader
 */

#include "load_common.hpp"

#include "error_code.h"
#include "memory_alloc.h"

#include <cassert>
#include <fstream>
#include <string>

namespace cubload
{

  void
  free_string (string_type **str)
  {
    if (str == NULL || *str == NULL)
      {
	return;
      }

    string_type *str_ = *str;

    if (str_->need_free_val)
      {
	free_and_init (str_->val);
      }
    if (str_->need_free_self)
      {
	free_and_init (str_);
	*str = NULL;
      }
  }

  void
  free_class_command_spec (class_command_spec_type **class_cmd_spec)
  {
    if (class_cmd_spec == NULL || *class_cmd_spec == NULL)
      {
	return;
      }

    string_type *attr, *arg;
    class_command_spec_type *class_cmd_spec_ = *class_cmd_spec;

    if (class_cmd_spec_->ctor_spec)
      {
	for (arg = class_cmd_spec_->ctor_spec->arg_list; arg; arg = arg->next)
	  {
	    free_string (&arg);
	  }

	free_string (&class_cmd_spec_->ctor_spec->id_name);
	free_and_init (class_cmd_spec_->ctor_spec);
      }

    for (attr = class_cmd_spec_->attr_list; attr; attr = attr->next)
      {
	free_string (&attr);
      }

    free_and_init (class_cmd_spec_);
    *class_cmd_spec = NULL;
  }

  int
  split (int batch_size, std::string &object_file_name, batch_handler &handler, int &total_batches)
  {
    int rows = 0;
    std::string class_line;
    std::string batch_buffer;
    std::ifstream object_file (object_file_name, std::fstream::in);

    if (!object_file)
      {
	// file does not exists on server, let client do the split operation
	return ER_FAILED;
      }

    assert (batch_size > 0);

    total_batches = 0;

    for (std::string line; std::getline (object_file, line);)
      {
	if (starts_with (line, "%id"))
	  {
	    // do nothing for now
	    continue;
	  }

	if (starts_with (line, "%class"))
	  {
	    // in case of class line collect remaining for current class
	    // and start new batch for the new class
	    handle_batch (class_line, batch_buffer, total_batches, handler);

	    // store new class line
	    class_line = line;

	    // rewind forward rows counter until batch is full
	    for (; (rows % batch_size) != 0; rows++)
	      ;

	    continue;
	  }

	// it is a line containing row data so append it
	batch_buffer.append (line);

	// it could be that a row is wrapped on the next line,
	// this means that the row ends on the last line that does not end with '+' (plus) character
	if (!ends_with (line, "+"))
	  {
	    // since std::getline eats end line character, add it back in order to make loaddb lexer happy
	    batch_buffer.append ("\n");

	    rows++;

	    // check if we have a full batch
	    if ((rows % batch_size) == 0)
	      {
		handle_batch (class_line, batch_buffer, total_batches, handler);
	      }
	  }
      }

    // collect remaining rows
    handle_batch (class_line, batch_buffer, total_batches, handler);

    object_file.close ();

    return NO_ERROR;
  }

  void
  handle_batch (std::string &class_line, std::string &batch, int &total_batches, batch_handler &handler)
  {
    if (batch.empty () || class_line.empty ())
      {
	return;
      }

    std::string buffer;
    buffer.reserve (class_line.size() + buffer.size() + 1); // 1 is for \n character

    buffer.append (class_line);
    buffer.append ("\n");
    buffer.append (batch);

    handler (buffer, ++total_batches);

    // prepare to start new batch for the class
    batch.clear ();
  }

  bool
  starts_with (const std::string &str, const std::string &prefix)
  {
    return str.size () >= prefix.size () && 0 == str.compare (0, prefix.size (), prefix);
  }

  bool
  ends_with (const std::string &str, const std::string &suffix)
  {
    return str.size () >= suffix.size () && 0 == str.compare (str.size () - suffix.size (), suffix.size (), suffix);
  }
} // namespace cubload
