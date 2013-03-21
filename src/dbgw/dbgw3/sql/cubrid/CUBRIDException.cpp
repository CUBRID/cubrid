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

#include "dbgw3/sql/cubrid/CUBRIDCommon.h"

namespace dbgw
{

  namespace sql
  {

    CUBRIDException::CUBRIDException(
        const _ExceptionContext &context) throw() :
      Exception(context)
    {
      switch (getInterfaceErrorCode())
        {
        case CCI_ER_CON_HANDLE:
        case CCI_ER_COMMUNICATION:
          setConnectionError(true);
          break;
        default:
          setConnectionError(false);
          break;
        }
    }

    CUBRIDException::~CUBRIDException() throw()
    {
    }

    CUBRIDException CUBRIDExceptionFactory::create(
        const std::string &errorMessage)
    {
      T_CCI_ERROR cciError =
      {
        DBGW_ER_NO_ERROR, ""
      };

      return CUBRIDExceptionFactory::create(-1, cciError, errorMessage);
    }

    CUBRIDException CUBRIDExceptionFactory::create(int nInterfaceErrorCode,
        const std::string &errorMessage)
    {
      T_CCI_ERROR cciError =
      {
        DBGW_ER_NO_ERROR, ""
      };

      return CUBRIDExceptionFactory::create(nInterfaceErrorCode, cciError,
          errorMessage);
    }

    CUBRIDException CUBRIDExceptionFactory::create(int nInterfaceErrorCode,
        T_CCI_ERROR &cciError, const std::string &errorMessage)
    {
      _ExceptionContext context =
      {
        DBGW_ER_INTERFACE_ERROR, nInterfaceErrorCode,
        "", "", false
      };

      std::stringstream buffer;
      buffer << "[" << context.nErrorCode << "]";

      if (cciError.err_code != DBGW_ER_NO_ERROR)
        {
          context.nInterfaceErrorCode = cciError.err_code;
          context.errorMessage = cciError.err_msg;
        }

      if (context.errorMessage == "")
        {
          char szBuffer[100];
          if (cci_get_err_msg(context.nInterfaceErrorCode, szBuffer, 100) == 0)
            {
              context.errorMessage = szBuffer;
            }
          else
            {
              context.errorMessage = errorMessage;
            }
        }

      buffer << "[" << context.nInterfaceErrorCode << "]";
      buffer << " " << context.errorMessage;
      context.what = buffer.str();
      return CUBRIDException(context);
    }

  }

}
