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
#include "dbgw3/sql/oracle/OracleHandle.h"

namespace dbgw
{

  namespace sql
  {

    _OracleContext::_OracleContext() :
      pOCIEnv(NULL), pOCIErr(NULL), pOCISvc(NULL)
    {
    }

    _OracleHandle::_OracleHandle() :
      m_pHandle(NULL), m_nType(-1)
    {
    }

    _OracleHandle::~_OracleHandle()
    {
      free();
    }

    void *_OracleHandle::alloc(OCIEnv *pOCIEnv, int nType)
    {
      free();

      m_nType = nType;

      if (nType == OCI_HTYPE_ENV)
        {
          if (OCIEnvCreate((OCIEnv **) &m_pHandle, OCI_THREADED | OCI_OBJECT,
              NULL, NULL, 0, NULL, 0, NULL))
            {
              Exception e = OracleExceptionFactory::create(
                  "Failed to create oci handle");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
        }
      else
        {
          int nResult = OCIHandleAlloc(pOCIEnv, (void **) &m_pHandle, m_nType,
              0, NULL);
          if (nResult != OCI_SUCCESS)
            {
              Exception e = OracleExceptionFactory::create(nResult,
                  "Failed to create oci handle");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
        }

      return m_pHandle;
    }

    void _OracleHandle::free()
    {
      if (m_pHandle != NULL && m_nType != -1)
        {
          OCIHandleFree(m_pHandle, m_nType);
        }
    }

    _OracleDesciptor::_OracleDesciptor() :
      m_pDescriptor(NULL), m_nType(-1)
    {
    }

    _OracleDesciptor::~_OracleDesciptor()
    {
      free();
    }

    void *_OracleDesciptor::alloc(OCIEnv *pOCIEnv, int nType)
    {
      free();

      m_nType = nType;

      sword nResult = OCIDescriptorAlloc(pOCIEnv, (void **) &m_pDescriptor,
          m_nType, 0, NULL);
      if (nResult != OCI_SUCCESS)
        {
          Exception e = OracleExceptionFactory::create(nResult,
              "Failed to create oci descriptor.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_pDescriptor;
    }

    void _OracleDesciptor::free()
    {
      if (m_pDescriptor != NULL && m_nType != -1)
        {
          OCIDescriptorFree(m_pDescriptor, m_nType);
        }
    }

  }

}
