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
log_page::operator== (const log_page &other)
{
  return hdr == other.hdr
	 && std::string (area, LOGAREA_SIZE) == std::string (other.area, LOGAREA_SIZE);
}

bool
log_hdrpage::operator== (const log_hdrpage &other)
{
  return logical_pageid == other.logical_pageid
	 && offset == other.offset
	 && flags == other.flags
	 && checksum == other.checksum;
}

log_page_owner::log_page_owner (const char *buffer)
{
  m_buffer = new char [db_log_page_size ()];
  memcpy (m_buffer, buffer, db_log_page_size ());
  m_log_page = reinterpret_cast<LOG_PAGE *> (m_buffer);
}

log_page_owner::~log_page_owner ()
{
  delete[] m_buffer;
}

bool
log_page_owner::operator== (const log_page_owner &other)
{
  return *m_log_page == * (other.m_log_page);
}

bool
log_page_owner::operator== (const LOG_PAGE &other)
{
  return *m_log_page == other;
}

const LOG_HDRPAGE &
log_page_owner::get_header () const
{
  return m_log_page->hdr;
}

LOG_PAGEID
log_page_owner::get_id () const
{
  return m_log_page->hdr.logical_pageid;
}