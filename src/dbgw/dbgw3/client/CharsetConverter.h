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

#ifndef CHARSETCONVERTER_H_
#define CHARSETCONVERTER_H_

namespace dbgw
{

  class Value;
  class _ValueSet;

  enum CodePage
  {
    DBGW_IDENTITY = 0,		/* no charset */

    /* DBGW3.0 supported character sets */
    DBGW_US_ASCII = 20127,	/* us-ascii (7 bit) */
    DBGW_UTF_8 = 65001,		/* utf-8 */
    DBGW_MS_932 = 932,		/* Microsoft Code Page 932 (Japanese) */
    DBGW_MS_949 = 949,		/* Microsoft Code Page 949 (Korean) */
    DBGW_MS_1252 = 1252,	/* Microsoft Code Page 1252 (Western Europe) */
    DBGW_EUC_JP = 51932,	/* euc-jp */
    DBGW_SHIFT_JIS = DBGW_MS_932,	/* shift-jis */
    DBGW_EUC_KR = DBGW_MS_949,	/* euc-kr */
    DBGW_LATIN_1 = DBGW_MS_1252,	/* latin1 */
    DBGW_ISO_8859_1 = DBGW_MS_1252	/* iso-8859-1 */
  };

  CodePage stringToCodepage (const std::string & charset);
  const char *codepageToString (CodePage code);

  class _CharsetConverter
  {
  public:
    _CharsetConverter (CodePage to, CodePage from);
    virtual ~ _CharsetConverter ();

    std::string convert (const std::string & value);
    void convert (_ValueSet & valueSet);
    void close ();

  private:
    void convert (Value * pValue);
    void convert (const char *szInBuf, size_t nInSize, char *szOutBuf,
		  size_t * pOutSize);

  private:
      CodePage m_toCodepage;
    CodePage m_fromCodepage;
    void *m_pHandle;
  };

}

#endif
