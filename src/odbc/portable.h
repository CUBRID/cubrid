#ifndef	__ODBC_PORTABLE_HEADER  /* to avoid multiple inclusion */
#define	__ODBC_PORTABLE_HEADER

#include		"windows.h"
#include		"sqlext.h"

#define	ODBC_INTERFACE	
#define	PUBLIC
#define	PRIVATE		static

#ifndef BUF_SIZE
#define	BUF_SIZE		1024 
#endif
#define		ITEMBUFLEN			128
#define		NAMEBUFLEN			128
#define		VALUEBUFLEN			256

#define		ERR_CODE	short
#define		_BOOL_		short

#define _TRUE_	1
#define _FALSE_	0

#define _OK_	0
#define _ERROR_ (-1)

#define ERROR_RETURN(err)	if ( err < 0 ) return err;
#define ERROR_GOTO(err, label)			if ( err < 0 ) goto label;


#define MAX(X, Y)               ((X) > (Y) ? (X) : (Y))
#define MIN(X, Y)				((X) < (Y) ? (X) : (Y))

/* --------------------------------------------------------------------
 *							Handle Management
 * -------------------------------------------------------------------- */
#define		INIT				0
#define		RESET				1
#define		FREE_MEMBER			2
#define		FREE_ALL			3


/* ----------------------------------------------------------------
 *                  CUBRID ODBC extension
 * ---------------------------------------------------------------- */

#define		ODBC_SUCCESS			SQL_SUCCESS
#define		ODBC_SUCCESS_WITH_INFO	SQL_SUCCESS_WITH_INFO
#define		ODBC_ERROR				SQL_ERROR
#define		ODBC_NO_DATA			SQL_NO_DATA
#define		ODBC_NEED_DATA			SQL_NEED_DATA
#define		ODBC_INVALID_HANDLE		SQL_INVALID_HANDLE
#define		ODBC_NTS				SQL_NTS
#define		ODBC_ROW_DELETED		(-101)

#if (ODBCVER >= 0x300)
#define		SQL_IS_BINARY		(-9)
#define		SQL_IS_STRING		(-10)

#define		SQL_POINTER			21
#define		SQL_C_POINTER		SQL_POINTER

#define		SQL_STRING			22
#define		SQL_C_STRING		SQL_STRING

#define		SQL_TYPE_EMPTY		23
#define		SQL_C_TYPE_EMPTY	SQL_TYPE_EMPTY
#endif

#define		ODBC_STRLEN_IND(value)		\
		((value == NULL) ? SQL_NULL_DATA : strlen(value))

extern PUBLIC HINSTANCE	hInstance;

#endif	/* ! __ODBC_PORTABLE_HEADER */
