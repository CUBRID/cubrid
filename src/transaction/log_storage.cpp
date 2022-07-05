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

#include "log_storage.hpp"

#include "log_impl.h"

bool
log_page::operator== (const log_page &other) const
{
  return hdr == other.hdr
	 && std::string (area, LOGAREA_SIZE) == std::string (other.area, LOGAREA_SIZE);
}

bool
log_hdrpage::operator== (const log_hdrpage &other) const
{
  return logical_pageid == other.logical_pageid
	 && offset == other.offset
	 && flags == other.flags
	 && checksum == other.checksum;
}

log_page_owner::log_page_owner (const char *buffer)
{
  m_buffer = std::string (buffer, LOG_PAGESIZE);
}

log_page_owner::~log_page_owner ()
{
}

bool
log_page_owner::operator== (const log_page_owner &other) const
{
  return *get_log_page () == *other.get_log_page ();
}

bool
log_page_owner::operator== (const LOG_PAGE &other) const
{
  return *get_log_page () == other;
}

LOG_PAGEID
log_page_owner::get_id () const
{
  return get_log_page ()->hdr.logical_pageid;
}

const LOG_PAGE *
log_page_owner::get_log_page () const
{
  return reinterpret_cast<const LOG_PAGE *> (m_buffer.c_str ());
}
