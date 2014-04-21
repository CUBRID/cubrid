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

#include "dbgw3/sql/oracle/OracleCommon.h"
#include "dbgw3/sql/oracle/OracleLob.h"

namespace dbgw
{

  namespace sql
  {

    OracleLob::OracleLob(_OracleContext *pContext,
        ValueType type) :
      m_pContext(pContext), m_pOCILob(NULL), m_type(type), m_nLength(-1),
      m_bIsFirstWrite(true), m_bIsExistTemp(false)
    {
      m_pOCILob = (OCILobLocator *) m_ociLob.alloc(m_pContext->pOCIEnv,
          OCI_DTYPE_LOB);
    }

    OracleLob::OracleLob(_OracleContext *pContext,
        ValueType type, OCILobLocator *pOCILob) :
      m_pContext(pContext), m_pOCILob(pOCILob), m_type(type), m_nLength(-1),
      m_bIsFirstWrite(true), m_bIsExistTemp(false)
    {
    }

    OracleLob::~OracleLob()
    {
      if (m_bIsExistTemp)
        {
          OCILobFreeTemporary(m_pContext->pOCISvc, m_pContext->pOCIErr,
              m_pOCILob);
        }
    }

    int64 OracleLob::length()
    {
      if ((int) m_nLength == -1 && m_pOCILob != NULL)
        {
          OCILobGetLength(m_pContext->pOCISvc, m_pContext->pOCIErr, m_pOCILob,
              &m_nLength);
        }

      return m_nLength;
    }

    void OracleLob::read(void **pValue, int64 lBufferSize,
        int64 *pReadSize)
    {
      ub4 amtp = 0;     /* streamed mode using pooling */

      sword nResult = OCILobRead(m_pContext->pOCISvc, m_pContext->pOCIErr,
          m_pOCILob, &amtp, 1, pValue, lBufferSize, NULL, NULL, 0,
          SQLCS_IMPLICIT);
      if (nResult != OCI_SUCCESS && nResult != OCI_NEED_DATA)
        {
          Exception e = OracleExceptionFactory::create(nResult,
              m_pContext->pOCIErr, "Failed to read lob object.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      *pReadSize = amtp;
    }

    void OracleLob::write(void *pValue, int64 lBufferSize,
        LobStatus lobStatus)
    {
      if (m_bIsExistTemp == false)
        {
          sword nResult = OCILobCreateTemporary(m_pContext->pOCISvc,
              m_pContext->pOCIErr, m_pOCILob, 0, SQLCS_IMPLICIT,
              m_type == DBGW_VAL_TYPE_CLOB ?
              OCI_TEMP_CLOB : OCI_TEMP_BLOB, OCI_ATTR_NOCACHE,
              OCI_DURATION_SESSION);
          if (nResult != OCI_SUCCESS)
            {
              Exception e = OracleExceptionFactory::create(nResult,
                  m_pContext->pOCIErr, "Failed to create lob object.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          m_bIsExistTemp = true;
        }

      ub4 amtp = 0;     /* streamed mode using pooling */
      ub1 nPiece = OCI_FIRST_PIECE;

      if (m_bIsFirstWrite)
        {
          if (lobStatus == DBGW_LOB_STATUS_DONE)
            {
              amtp = lBufferSize;
              nPiece = OCI_ONE_PIECE;
            }
        }
      else
        {
          if (lobStatus == DBGW_LOB_STATUS_DONE)
            {
              nPiece = OCI_LAST_PIECE;
            }
          else
            {
              nPiece = OCI_NEXT_PIECE;
            }
        }

      sword nResult = OCILobWrite(m_pContext->pOCISvc, m_pContext->pOCIErr,
          m_pOCILob, &amtp, 1, pValue, (ub4) lBufferSize, nPiece, NULL,
          NULL, 0, SQLCS_IMPLICIT);
      if (nResult != OCI_SUCCESS && nResult != OCI_NEED_DATA)
        {
          Exception e = OracleExceptionFactory::create(nResult,
              m_pContext->pOCIErr, "Failed to read lob object.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      m_bIsFirstWrite = false;
    }

    void *OracleLob::getNativeHandle() const
    {
      return (void *) m_pOCILob;
    }

  }

}
