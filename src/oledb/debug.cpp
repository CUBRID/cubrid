/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

#include "stdafx.h"
#include "debug.h"

void show_error(LPSTR msg, int code, T_CCI_ERROR *error)
{
	ATLTRACE(atlTraceDBProvider, 2, "Error : %s [code : %d]", msg, code);
    if (code == CCI_ER_DBMS)
	{
		ATLTRACE(atlTraceDBProvider, 2, "DBMS Error : %s [code : %d]",
			(LPSTR)error->err_msg, error->err_code);
    }
}

T_CCI_A_TYPE GetCASTypeA(DBTYPE type)
{
	switch (type)
	{
	case DBTYPE_I1 : return CCI_A_TYPE_INT;
	case DBTYPE_I2 : return CCI_A_TYPE_INT;
	case DBTYPE_R4 : return CCI_A_TYPE_FLOAT;
	case DBTYPE_R8 : return CCI_A_TYPE_DOUBLE;
	case DBTYPE_WSTR :
	case DBTYPE_STR : return CCI_A_TYPE_STR;
	}
	return CCI_A_TYPE_FIRST;
}

T_CCI_U_TYPE GetCASTypeU(DBTYPE type)
{
	switch (type)
	{
	case DBTYPE_I1 : return CCI_U_TYPE_SHORT;
	case DBTYPE_I2 : return CCI_U_TYPE_INT;
	case DBTYPE_R4 : return CCI_U_TYPE_FLOAT;
	case DBTYPE_R8 : return CCI_U_TYPE_DOUBLE;
	case DBTYPE_WSTR :
	case DBTYPE_STR : return CCI_U_TYPE_STRING;			  
	}
	return CCI_U_TYPE_NULL;
}

DBTYPE GetOledbTypeFromName(LPOLESTR wszName)
{
	if (wszName == NULL)
		return DBTYPE_WSTR;

	if (_wcsicmp(wszName, L"INTEGER(1)") == 0 ||
		_wcsicmp(wszName, L"TINYINT")  == 0 ||
		_wcsicmp(wszName, L"DBTYPE_I1")  == 0)
	{
		return DBTYPE_I1;
	}

	if (_wcsicmp(wszName, L"INTEGER(2)") == 0 ||
		_wcsicmp(wszName, L"AUTOINC(2)") == 0 ||
		_wcsicmp(wszName, L"SMALLINT") == 0 ||
		_wcsicmp(wszName, L"SMALLINDENTITY") == 0 ||
		_wcsicmp(wszName, L"YEAR") == 0 ||
		_wcsicmp(wszName, L"DBTYPE_I2")  == 0)
	{
		return DBTYPE_I2;
	}

	if (_wcsicmp(wszName, L"INTEGER(4)") == 0 ||
		_wcsicmp(wszName, L"AUTOINC(4)") == 0 ||
		_wcsicmp(wszName, L"INTEGER")	 == 0 ||
		_wcsicmp(wszName, L"MEDIUMINT")	 == 0 ||
		_wcsicmp(wszName, L"INT")		 == 0 ||
		_wcsicmp(wszName, L"IDENTITY")	 == 0 ||
		_wcsicmp(wszName, L"DBTYPE_I4")	 == 0)
	{
		return DBTYPE_I4;
	}

	if (_wcsicmp(wszName, L"INTEGER(8)") == 0 ||
		_wcsicmp(wszName, L"BIGINT")	 == 0 ||
		_wcsicmp(wszName, L"DBTYPE_I8")  == 0)
	{
		return DBTYPE_I8;
	}

	if (_wcsicmp(wszName, L"FLOAT(4)")  == 0 ||
		_wcsicmp(wszName, L"BFLOAT(4)") == 0 ||
		_wcsicmp(wszName, L"REAL")		== 0 ||
		_wcsicmp(wszName, L"FLOAT")		== 0 ||
		_wcsicmp(wszName, L"FLOAT UNSIGNED")== 0 ||
		_wcsicmp(wszName, L"DBTYPE_R4") == 0)
	{
		return DBTYPE_R4;
	}

	if (_wcsicmp(wszName, L"FLOAT(8)")		== 0 ||
		_wcsicmp(wszName, L"BFLOAT(8)")		== 0 ||
		_wcsicmp(wszName, L"MONEY")			== 0 ||
		_wcsicmp(wszName, L"DECIMAL")		== 0 ||
		_wcsicmp(wszName, L"DOUBLE")		== 0 ||
		_wcsicmp(wszName, L"DOUBLE UNSIGNED")	== 0 ||
		_wcsicmp(wszName, L"DOUBLE PRECISION")	== 0 ||
		_wcsicmp(wszName, L"NUMERIC")		== 0 ||
		_wcsicmp(wszName, L"NUMERICSA")		== 0 ||
		_wcsicmp(wszName, L"NUMERICSTS")	== 0 ||
		_wcsicmp(wszName, L"DBTYPE_R8")		== 0)
	{
		return DBTYPE_R8;
	}


	if (_wcsicmp(wszName, L"CURRENCY")  == 0 ||
		_wcsicmp(wszName, L"DBTYPE_CY") == 0)
	{
		return DBTYPE_CY;
	}

	if (_wcsicmp(wszName, L"LOGICAL")	  == 0 ||
		_wcsicmp(wszName, L"BIT")		  == 0 ||
		_wcsicmp(wszName, L"DBTYPE_BOOL") == 0)
	{
		return DBTYPE_BOOL;
	}


	if (_wcsicmp(wszName, L"UNSIGNED(1)") == 0 ||
		_wcsicmp(wszName, L"DBTYPE_UI1")  == 0 ||
		_wcsicmp(wszName, L"UTINYINT")  == 0   ||
		_wcsicmp(wszName, L"TINYINT UNSIGNED")  == 0)
	{
		return DBTYPE_UI1;
	}

	if (_wcsicmp(wszName, L"UNSIGNED(2)") == 0 ||
		_wcsicmp(wszName, L"DBTYPE_UI2")  == 0 ||
		_wcsicmp(wszName, L"USMALLINT")  == 0  ||
		_wcsicmp(wszName, L"SMALLINT UNSIGNED")  == 0 )
	{
		return DBTYPE_UI2;
	}

	if (_wcsicmp(wszName, L"UNSIGNED(4)") == 0 ||
		_wcsicmp(wszName, L"DBTYPE_UI4")  == 0 ||
		_wcsicmp(wszName, L"UINTEGER")  == 0   ||
		_wcsicmp(wszName, L"INTEGER UNSIGNED")  == 0   ||
		_wcsicmp(wszName, L"MEDIUMINT UNSIGNED")  == 0   )
	{
		return DBTYPE_UI4;
	}

	if (_wcsicmp(wszName, L"UNSIGNED(8)") == 0 ||
		_wcsicmp(wszName, L"DBTYPE_UI8")  == 0 ||
		_wcsicmp(wszName, L"BIGINT UNSIGNED")  == 0 ||
		_wcsicmp(wszName, L"UBIGINT")  == 0)
	{
		return DBTYPE_UI8;
	}

	if (_wcsicmp(wszName, L"CHARACTER") == 0 ||
		_wcsicmp(wszName, L"LSTRING")	== 0 ||
		_wcsicmp(wszName, L"ZSTRING")	== 0 ||
		_wcsicmp(wszName, L"NOTE")		== 0 ||
		_wcsicmp(wszName, L"CHAR")		== 0 ||
		_wcsicmp(wszName, L"VARCHAR")		== 0 ||
		_wcsicmp(wszName, L"LONGVARCHAR")    == 0 ||
		_wcsicmp(wszName, L"NULL")    == 0 ||
		_wcsicmp(wszName, L"ENUM")		== 0 ||
		_wcsicmp(wszName, L"SET")		== 0 ||
		_wcsicmp(wszName, L"DBTYPE_STR") == 0)
	{
		return DBTYPE_STR;
	}

	if (_wcsicmp(wszName, L"TINYBLOB")		== 0 ||
		_wcsicmp(wszName, L"TINYTEXT")		== 0 ||
		_wcsicmp(wszName, L"BLOB")		== 0 ||
		_wcsicmp(wszName, L"TEXT")		== 0 ||
		_wcsicmp(wszName, L"MEDIUMBLOB")		== 0 ||
		_wcsicmp(wszName, L"MEDIUMTEXT")		== 0 ||
		_wcsicmp(wszName, L"LONGBLOB")		== 0 ||
		_wcsicmp(wszName, L"LONGTEXT")		== 0 )
	{
		return DBTYPE_STR;
	}

	if (_wcsicmp(wszName, L"LVAR")			== 0 ||
		_wcsicmp(wszName, L"LONG VARBINARY") == 0 ||
		_wcsicmp(wszName, L"LONGVARBINARY") == 0 ||
		_wcsicmp(wszName, L"BINARY") == 0 ||
		_wcsicmp(wszName, L"DBTYPE_BYTES")  == 0)
	{
		return DBTYPE_BYTES;
	}

	if (_wcsicmp(wszName, L"DATE")			== 0 ||
		_wcsicmp(wszName, L"DBTYPE_DBDATE") == 0)
	{
		return DBTYPE_DBDATE;
	}

	if (_wcsicmp(wszName, L"TIME")			== 0 ||
		_wcsicmp(wszName, L"DBTYPE_DBTIME") == 0)
	{
		return DBTYPE_DBTIME;
	}

	if (_wcsicmp(wszName, L"TIMESTAMP")			 == 0 ||
		_wcsicmp(wszName, L"DATETIME")			 == 0 ||
		_wcsicmp(wszName, L"DBTYPE_DBTIMESTAMP") == 0)
	{
		return DBTYPE_DBTIMESTAMP;
	}

	return DBTYPE_STR;
}
