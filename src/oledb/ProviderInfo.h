#pragma once

namespace ProvInfo {
	extern int DEFAULT_FETCH_SIZE;

	// Buffer with all used strings. 
	extern WCHAR wszAllStrings[];
	extern int size_wszAllStrings;

	// Relative strings
	extern LPOLESTR pwszPoint;
	extern LPOLESTR pwszPercent;
	extern LPOLESTR pwszUnderline;
	extern LPOLESTR pwszLeftSqBracket;
	extern LPOLESTR pwszRightSqBracket;
	extern LPOLESTR pwszQuote;
	extern LPOLESTR pwszInvalidChars;
	extern LPOLESTR pwszInvalidFirstChars;
	extern LPOLESTR pwszInvalidCharsShort;
	extern LPOLESTR pwszInvalidFirstCharsShort;

	extern DBLITERALINFO LiteralInfos[];
	extern int size_LiteralInfos;

	typedef struct
	{
		LPWSTR szName;
		T_CCI_U_TYPE nCCIUType;
		LPWSTR szCreateParams;
	} _ptypes;

	extern _ptypes provider_types[];
	extern int size_provider_types;

}//end of namespace ProvInfo
