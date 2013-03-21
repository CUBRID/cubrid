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

#if defined(WINDOWS) || defined(_WIN32) || defined(_WIN64)
#else
#include <iconv.h>
#endif
#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Value.h"
#include "dbgw3/ValueSet.h"
#include "dbgw3/system/DBGWPorting.h"
#include "dbgw3/client/CharsetConverter.h"

namespace dbgw
{

  _CharsetConverter::_CharsetConverter(CodePage to, CodePage from) :
    m_toCodepage(to), m_fromCodepage(from), m_pHandle(NULL)
  {
#if defined(WINDOWS) || defined(_WIN32) || defined(_WIN64)
#else
    m_pHandle = iconv_open(codepageToString(to), codepageToString(from));
    if (m_pHandle == (iconv_t)(-1))
      {
        CreateConverterFailException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
#endif
  }

  _CharsetConverter::~_CharsetConverter()
  {
    close();
  }

  void _CharsetConverter::convert(const char *szInBuf, size_t nInSize,
      char *szOutBuf, size_t *pOutSize)
  {
    size_t nLen;
#if defined(WINDOWS) || defined(_WIN32) || defined(_WIN64)
    std::auto_ptr<wchar_t> wc(new wchar_t[nInSize + 1]);

    int nSize = MultiByteToWideChar((UINT) m_fromCodepage, 0, szInBuf, -1,
        wc.get(), (int) nInSize + 1);
    if (nSize == 0)
      {
        ConvertCharsetFailException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    nLen = WideCharToMultiByte((UINT) m_toCodepage, 0, wc.get(), nSize,
        szOutBuf, (int) pOutSize, NULL, NULL);
    if (nLen == 0)
      {
        ConvertCharsetFailException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    szOutBuf[nLen] = '\0';
#else
    char *szIn = (char *) szInBuf;
    char *szOut = szOutBuf;

    if (m_pHandle != NULL)
      {
        nLen = iconv(m_pHandle, &szIn, &nInSize, &szOut, pOutSize);
        if (nLen == (size_t)(-1))
          {
            ConvertCharsetFailException e;
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        *szOut = '\0';
      }
#endif
  }

  std::string _CharsetConverter::convert(const std::string &value)
  {
    size_t nInSize = value.length();
    size_t nOutSize = nInSize * 4 + 1;
    char *szTempBuffer = new char[nOutSize];

    try
      {
        convert(value.c_str(), nInSize, szTempBuffer, &nOutSize);
      }
    catch (...)
      {
        delete[] szTempBuffer;
        throw;
      }

    std::string outValue = szTempBuffer;
    delete[] szTempBuffer;
    return outValue;
  }

  void _CharsetConverter::convert(Value *pValue)
  {
    ValueType type = pValue->getType();

    if (type != DBGW_VAL_TYPE_STRING)
      {
        MismatchValueTypeException e(type, DBGW_VAL_TYPE_STRING);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    if (pValue->isNull())
      {
        return;
      }

    const char *szValue = NULL;
    if (pValue->getCString(&szValue) == false)
      {
        throw getLastException();
      }

    size_t nInSize = pValue->getLength();
    size_t nOutSize = nInSize * 4 + 1;
    char *szTempBuffer = new char[nOutSize];

    try
      {
        convert(szValue, nInSize, szTempBuffer, &nOutSize);

        pValue->set(DBGW_VAL_TYPE_STRING, szTempBuffer, false, -1);
      }
    catch (...)
      {
        delete[] szTempBuffer;
        throw;
      }

    delete[] szTempBuffer;
  }

  void _CharsetConverter::convert(_ValueSet &valueSet)
  {
    for (size_t i = 0; i < valueSet.size(); i++)
      {
        Value *pValue = valueSet[i];
        if (pValue == NULL || pValue->getType() != DBGW_VAL_TYPE_STRING)
          {
            continue;
          }

        convert(pValue);
      }
  }

  void _CharsetConverter::close()
  {
#if defined(WINDOWS) || defined(_WIN32) || defined(_WIN64)
#else
    if (m_pHandle != NULL)
      {
        iconv_close(m_pHandle);
        m_pHandle = NULL;
      }
#endif
  }

  CodePage stringToCodepage(const char *charset)
  {
    if (!strcasecmp(charset, "US-ASCII"))
      {
        return DBGW_US_ASCII;
      }
    else if (!strcasecmp(charset, "UTF-8"))
      {
        return DBGW_UTF_8;
      }
    else if (!strcasecmp(charset, "MS-932"))
      {
        return DBGW_MS_932;
      }
    else if (!strcasecmp(charset, "MS-949"))
      {
        return DBGW_MS_949;
      }
    else if (!strcasecmp(charset, "MSCP949"))
      {
        return DBGW_MS_949;
      }
    else if (!strcasecmp(charset, "MS-1252"))
      {
        return DBGW_MS_1252;
      }
    else if (!strcasecmp(charset, "ECU-JP"))
      {
        return DBGW_EUC_JP;
      }
    else if (!strcasecmp(charset, "SHIFT-JIS"))
      {
        return DBGW_SHIFT_JIS;
      }
    else if (!strcasecmp(charset, "EUC-KR"))
      {
        return DBGW_EUC_KR;
      }
    else if (!strcasecmp(charset, "LATIN1"))
      {
        return DBGW_LATIN_1;
      }
    else if (!strcasecmp(charset, "ISO-8859-1"))
      {
        return DBGW_ISO_8859_1;
      }
    else
      {
        InvalidCharsetException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  const char *codepageToString(CodePage code)
  {
    switch ((int)code)
      {
      case DBGW_US_ASCII:
      {
        return "US-ASCII";
      }
      case DBGW_UTF_8:
      {
        return "UTF-8";
      }
      case DBGW_MS_932:
      {
        return "CP932";
      }
      case DBGW_MS_949:
      {
        return "CP949";
      }
      case DBGW_MS_1252:
      {
        return "CP1252";
      }
      case DBGW_EUC_JP:
      {
        return "EUC-JP";
      }
      }

    return NULL;
  }

}
