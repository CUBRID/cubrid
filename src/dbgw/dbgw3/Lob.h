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

#ifndef LOB_H_
#define LOB_H_

namespace dbgw
{

  enum LobStatus
  {
    DBGW_LOB_STATUS_IN_PROGRESS,
    DBGW_LOB_STATUS_DONE
  };

  class Lob
  {
  public:
    Lob() {}
    virtual ~Lob() {}

    virtual int64 length() = 0;
    virtual void read(void **pValue, int64 lBufferSize, int64 *pReadSize) = 0;
    virtual void write(void *pValue, int64 lBufferSize,
        LobStatus lobStatus = DBGW_LOB_STATUS_IN_PROGRESS) = 0;

  public:
    virtual void *getNativeHandle() const = 0;
  };

}

#endif
