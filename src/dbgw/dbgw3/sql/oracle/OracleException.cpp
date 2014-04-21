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

namespace dbgw
{

  namespace sql
  {

    Exception OracleExceptionFactory::create(const std::string &errorMessage)
    {
      return create(-1, NULL, errorMessage);
    }

    Exception OracleExceptionFactory::create(int nInterfaceErrorCode,
        const std::string &errorMessage)
    {
      return create(nInterfaceErrorCode, NULL, errorMessage);
    }

    Exception OracleExceptionFactory::create(int nInterfaceErrorCode,
        OCIError *pOCIErr, const std::string &errorMessage)
    {
      _ExceptionContext context =
      {
        DBGW_ER_INTERFACE_ERROR, nInterfaceErrorCode, errorMessage, "",
        false
      };

      std::stringstream buffer;
      buffer << "[" << context.nErrorCode << "]";

      if (nInterfaceErrorCode == OCI_ERROR && pOCIErr != NULL)
        {
          text szBuffer[512];
          sb4 nErrorCode;
          int nError = OCIErrorGet(pOCIErr, 1, (text *) NULL, &nErrorCode,
              szBuffer, sizeof(szBuffer), OCI_HTYPE_ERROR);
          if (nError == OCI_SUCCESS)
            {
              context.nInterfaceErrorCode = nErrorCode;
              context.errorMessage = (char *) szBuffer;
              context.errorMessage = context.errorMessage.substr(0,
                  context.errorMessage.length() - 1);
            }
        }

      buffer << "[" << context.nInterfaceErrorCode << "]";
      buffer << " " << context.errorMessage;
      context.what = buffer.str();
      return Exception(context);
    }

  }

}
