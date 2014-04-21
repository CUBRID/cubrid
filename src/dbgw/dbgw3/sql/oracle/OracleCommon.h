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

#ifndef ORACLECOMMON_H_
#define ORACLECOMMON_H_

#include <oci.h>
#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Lob.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/sql/oracle/OracleMock.h"
#include "dbgw3/sql/oracle/OracleHandle.h"
#include "dbgw3/sql/oracle/OracleException.h"
#include "dbgw3/sql/oracle/OracleLob.h"

namespace dbgw
{

  namespace sql
  {

    namespace oci
    {

      const static int MAX_LEN_INT64 = 22;
      const static int MAX_LEN_NUMBER = 128;
      const static int MAX_LEN_ROWID = 128;
      const static int MAX_LEN_LONG = 16777216;
      const static int MAX_LEN_OUT_BIND_STRING = MAX_LEN_LONG;
      const static int MAX_LEN_OUT_BIND_BYTES = MAX_LEN_LONG;
      const static int SIZE_OCIDATE = 7;

      static void setOCIDateTime(char *szBuffer, sb2 nYear, ub1 nMonth,
          ub1 nDay, ub1 nHour, ub1 nMin, ub1 nSec)
      {
        if (szBuffer == NULL)
          {
            return;
          }

        unsigned char cc = (unsigned char)(nYear / 100);
        unsigned char yy = (unsigned char)(nYear - nYear / 100 * 100);
        szBuffer[0] = (ub1) abs(cc + 100);
        szBuffer[1] = (ub1) abs(yy + 100);
        szBuffer[2] = nMonth;
        szBuffer[3] = nDay;
        szBuffer[4] = nHour + 1;
        szBuffer[5] = nMin + 1;
        szBuffer[6] = nSec + 1;;
      }

      static void getOCIDate(char *szBuffer, sb2 *pYear, ub1 *pMonth,
          ub1 *pDay)
      {
        unsigned char cc = (unsigned char) abs(szBuffer[0] - 100);
        unsigned char yy = (unsigned char) abs(szBuffer[1] - 100);
        *pMonth = szBuffer[2];
        *pDay = szBuffer[3];
        *pYear = (sb2) cc *100 + (sb2) yy;
      }

      static void getOCITime(char *szBuffer, ub1 *pHour, ub1 *pMin, ub1 *pSec)
      {
        *pHour = szBuffer[4] - 1;
        *pMin = szBuffer[5] - 1;
        *pSec = szBuffer[6] - 1;;
      }

    }

  }

}

#endif
