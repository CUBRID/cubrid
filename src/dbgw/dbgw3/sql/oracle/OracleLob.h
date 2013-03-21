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

#ifndef ORACLELOB_H_
#define ORACLELOB_H_

namespace dbgw
{

  namespace sql
  {

    class OracleLob : public Lob
    {
    public:
      OracleLob(_OracleContext *pContext, ValueType type);
      OracleLob(_OracleContext *pContext, ValueType type,
          OCILobLocator *pOCILob);
      virtual ~OracleLob();

      virtual int64 length();
      virtual void read(void **pValue, int64 lBufferSize, int64 *pReadSize);
      virtual void write(void *pValue, int64 lBufferSize,
          LobStatus lobStatus = DBGW_LOB_STATUS_IN_PROGRESS);

    public:
      virtual void *getNativeHandle() const;

    private:
      _OracleContext *m_pContext;
      _OracleDesciptor m_ociLob;
      OCILobLocator *m_pOCILob;
      ValueType m_type;
      ub4 m_nLength;
      bool m_bIsFirstWrite;
      bool m_bIsExistTemp;
    };

  }

}

#endif
