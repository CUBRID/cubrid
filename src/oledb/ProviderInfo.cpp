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
#include "ProviderInfo.h"

namespace ProvInfo {
	int DEFAULT_FETCH_SIZE = 100;

	WCHAR wszAllStrings[] = L".\0" // pwszPoint
				L"%\0" // pwszPercent
				L"_\0" // pwszUnderline
				L"[\0" // pwszLeftSqBracket
				L"]\0" // pwszRightSqBracket
				L"\"\0" // pwszQuote
				L"!\"%&'()*+,-./:;<=>?@[\\]^{|} ~\0" // pwszInvalidChars
				L"0123456789!\"%&'()*+,-./:;<=>?@[\\]^{|}~ \0" // pwszInvalidFirstChars
				L"\",.;*<>?|\0" // pwszInvalidCharsShort
				L"0123456789\",.;*<>?|\0"; // pwszInvalidFirstCharsShort
	int size_wszAllStrings = sizeof(wszAllStrings);

	// Relative strings
	LPOLESTR pwszPoint = wszAllStrings;
	LPOLESTR pwszPercent = pwszPoint + wcslen( pwszPoint ) + 1;
	LPOLESTR pwszUnderline = pwszPercent + wcslen( pwszPercent ) + 1;
	LPOLESTR pwszLeftSqBracket = pwszUnderline + wcslen( pwszUnderline ) + 1;
	LPOLESTR pwszRightSqBracket = pwszLeftSqBracket + wcslen( pwszLeftSqBracket ) + 1;
	LPOLESTR pwszQuote = pwszRightSqBracket + wcslen( pwszRightSqBracket ) + 1;
	LPOLESTR pwszInvalidChars = pwszQuote + wcslen( pwszQuote ) + 1;
	LPOLESTR pwszInvalidFirstChars = pwszInvalidChars + wcslen( pwszInvalidChars ) + 1;
	LPOLESTR pwszInvalidCharsShort = pwszInvalidFirstChars + wcslen( pwszInvalidFirstChars ) + 1;
	LPOLESTR pwszInvalidFirstCharsShort = pwszInvalidCharsShort + wcslen( pwszInvalidCharsShort ) + 1;

	DBLITERALINFO LiteralInfos[] = {
		{ NULL, NULL, NULL, DBLITERAL_BINARY_LITERAL, TRUE, 16384 },
		//{ NULL, NULL, NULL, DBLITERAL_CATALOG_NAME, FALSE, 0 },
		//{ NULL, NULL, NULL, DBLITERAL_CATALOG_SEPARATOR, FALSE, 0 },
		{ NULL, NULL, NULL, DBLITERAL_CHAR_LITERAL, TRUE, 16384 },
		//{ NULL, NULL, NULL, DBLITERAL_COLUMN_ALIAS, FALSE, 0 },
		{ NULL, pwszInvalidChars, pwszInvalidFirstChars, DBLITERAL_COLUMN_NAME, TRUE, 127 },
		//{ NULL, NULL, NULL, DBLITERAL_COLUMN_ALIAS, DBLITERAL_CONSTRAINT_NAME , 0 },
		{ NULL, pwszInvalidChars, pwszInvalidFirstChars, DBLITERAL_CORRELATION_NAME, TRUE, 255 },
		{ NULL, pwszInvalidChars, pwszInvalidFirstChars, DBLITERAL_CURSOR_NAME, TRUE, 255 },
		//{ NULL, NULL, NULL, DBLITERAL_ESCAPE_PERCENT_PREFIX, FALSE, 0 },
		//{ NULL, NULL, NULL, DBLITERAL_ESCAPE_PERCENT_SUFFIX, FALSE, 0 },
		//{ NULL, NULL, NULL, DBLITERAL_ESCAPE_UNDERSCORE_PREFIX, FALSE, 0 },
		//{ NULL, NULL, NULL, DBLITERAL_ESCAPE_UNDERSCORE_SUFFIX, FALSE, 0 },
		{ NULL, pwszInvalidChars, pwszInvalidFirstChars, DBLITERAL_INDEX_NAME, TRUE, 255 },
		{ pwszPercent, NULL, NULL, DBLITERAL_LIKE_PERCENT, TRUE, 1 },
		{ pwszUnderline, NULL, NULL, DBLITERAL_LIKE_UNDERSCORE, TRUE, 1 },
		//{ NULL, NULL, NULL, DBLITERAL_PROCEDURE_NAME, FALSE, 0 },
		//{ NULL, NULL, NULL, DBLITERAL_SCHEMA_NAME, FALSE, 0 },
		//{ NULL, NULL, NULL, DBLITERAL_SCHEMA_SEPARATOR, FALSE, 0 },
		{ NULL, pwszInvalidChars, pwszInvalidFirstChars, DBLITERAL_TABLE_NAME, TRUE, 127 },
		{ NULL, NULL, NULL, DBLITERAL_TEXT_COMMAND, TRUE, ~0 },
		{ NULL, pwszInvalidChars, pwszInvalidFirstChars, DBLITERAL_USER_NAME, TRUE, 255 },
		{ NULL, pwszInvalidChars, pwszInvalidFirstChars, DBLITERAL_VIEW_NAME, TRUE, 255 },
		{ pwszQuote, NULL, NULL, DBLITERAL_QUOTE_PREFIX, TRUE, 1 },
		{ pwszQuote, NULL, NULL, DBLITERAL_QUOTE_SUFFIX, TRUE, 1 },
		};
	int size_LiteralInfos = sizeof(LiteralInfos)/sizeof(*LiteralInfos);
	// FIXME: 여기에 STRING이나, CreateParams가 없는 경우등 가능한 모든 alias들도 써주는게 맞을까?
	_ptypes provider_types[] = {
		{ L"SMALLINT", CCI_U_TYPE_SHORT, L"" },
		{ L"INT", CCI_U_TYPE_INT, L"" },
		{ L"CHAR", CCI_U_TYPE_CHAR, L"length" },
		{ L"VARCHAR", CCI_U_TYPE_STRING, L"max length" },
		{ L"NCHAR", CCI_U_TYPE_NCHAR, L"length" },
		{ L"NCHAR VARYING", CCI_U_TYPE_VARNCHAR, L"max length" },
		{ L"BIT", CCI_U_TYPE_BIT, L"length" },
		{ L"BIT VARYING", CCI_U_TYPE_VARBIT, L"max length" },
		//{ L"NUMERIC", CCI_U_TYPE_NUMERIC, L"precision,scale" },
		{ L"FLOAT", CCI_U_TYPE_FLOAT, L"" },
		{ L"DOUBLE", CCI_U_TYPE_DOUBLE, L"" },
		{ L"MONETARY", CCI_U_TYPE_MONETARY, L"" },
		{ L"DATE", CCI_U_TYPE_DATE, L"" },
		{ L"TIME", CCI_U_TYPE_TIME, L"" },
		{ L"TIMESTAMP", CCI_U_TYPE_TIMESTAMP, L"" },
	};
	int size_provider_types = sizeof(provider_types)/sizeof(*provider_types);
}