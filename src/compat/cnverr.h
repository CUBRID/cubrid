/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * cnverr.h - Error conditions for string conversion functions.
 */

#ifndef _CNVERR_H_
#define _CNVERR_H_

#ident "$Id$"

#include "condition_handler_code.h"

/* conversion error code */
#define CNV_ERR_STRING_TOO_LONG   CO_CODE( CO_MODULE_CNV, 1)
#define CNV_ERR_BAD_TYPE          CO_CODE( CO_MODULE_CNV, 2)
#define CNV_ERR_BAD_CHAR          CO_CODE( CO_MODULE_CNV, 3)
#define CNV_ERR_NO_DECIMAL        CO_CODE( CO_MODULE_CNV, 4)
#define CNV_ERR_NO_SIGN           CO_CODE( CO_MODULE_CNV, 5)
#define CNV_ERR_BAD_LEADING       CO_CODE( CO_MODULE_CNV, 6)
#define CNV_ERR_BAD_TRAILING      CO_CODE( CO_MODULE_CNV, 7)
#define CNV_ERR_BAD_THOUS         CO_CODE( CO_MODULE_CNV, 8)
#define CNV_ERR_EXTRA_INTEGER     CO_CODE( CO_MODULE_CNV, 9)
#define CNV_ERR_EXTRA_FRACTION    CO_CODE( CO_MODULE_CNV, 10)
#define CNV_ERR_MISSING_INTEGER   CO_CODE( CO_MODULE_CNV, 11)
#define CNV_ERR_MISSING_FRACTION  CO_CODE( CO_MODULE_CNV, 12)
#define CNV_ERR_EMPTY_STRING      CO_CODE( CO_MODULE_CNV, 13)
#define CNV_ERR_BAD_POSITION      CO_CODE( CO_MODULE_CNV, 14)
#define CNV_ERR_BAD_NULL          CO_CODE( CO_MODULE_CNV, 15)
#define CNV_ERR_EXTRA_SIGN        CO_CODE( CO_MODULE_CNV, 16)
#define CNV_ERR_INTEGER_OVERFLOW  CO_CODE( CO_MODULE_CNV, 17)
#define CNV_ERR_INTEGER_UNDERFLOW  CO_CODE( CO_MODULE_CNV, 18)
#define CNV_ERR_BAD_PATTERN       CO_CODE( CO_MODULE_CNV, 19)
#define CNV_ERR_BAD_X_DIGITS      CO_CODE( CO_MODULE_CNV, 20)
#define CNV_ERR_NO_CURRENCY       CO_CODE( CO_MODULE_CNV, 21)
#define CNV_ERR_NOT_UNIQUE        CO_CODE( CO_MODULE_CNV, 22)
#define CNV_ERR_BAD_DATE          CO_CODE( CO_MODULE_CNV, 23)
#define CNV_ERR_BAD_YEAR          CO_CODE( CO_MODULE_CNV, 24)
#define CNV_ERR_BAD_MONTH         CO_CODE( CO_MODULE_CNV, 25)
#define CNV_ERR_BAD_MDAY          CO_CODE( CO_MODULE_CNV, 26)
#define CNV_ERR_BAD_WDAY          CO_CODE( CO_MODULE_CNV, 27)
#define CNV_ERR_FLOAT_OVERFLOW    CO_CODE( CO_MODULE_CNV, 28)
#define CNV_ERR_FLOAT_UNDERFLOW   CO_CODE( CO_MODULE_CNV, 29)
#define CNV_ERR_UNKNOWN_DATE      CO_CODE( CO_MODULE_CNV, 30)
#define CNV_ERR_BAD_TIME          CO_CODE( CO_MODULE_CNV, 31)
#define CNV_ERR_BAD_HOUR          CO_CODE( CO_MODULE_CNV, 32)
#define CNV_ERR_BAD_MIN           CO_CODE( CO_MODULE_CNV, 33)
#define CNV_ERR_BAD_SEC           CO_CODE( CO_MODULE_CNV, 34)
#define CNV_ERR_BAD_AM_PM         CO_CODE( CO_MODULE_CNV, 35)
#define CNV_ERR_BAD_TIMESTAMP         CO_CODE( CO_MODULE_CNV, 36)
#define CNV_ERR_TIMESTAMP_UNDERFLOW   CO_CODE( CO_MODULE_CNV, 37)
#define CNV_ERR_TIMESTAMP_OVERFLOW    CO_CODE( CO_MODULE_CNV, 38)
#define CNV_ERR_BAD_FORMAT        CO_CODE( CO_MODULE_CNV, 39)

/* conversion error message format */
#define CNV_ER_FMT_STRING_TOO_LONG \
  "Value string longer than %d characters."
#define CNV_ER_FMT_BAD_TYPE \
  "Can't convert value of type `%s' to a string."
#define CNV_ER_FMT_BAD_CHAR \
  "Format error -- `%s' is not allowed."
#define CNV_ER_FMT_NO_DECIMAL \
  "Format error -- decimal point missing."
#define CNV_ER_FMT_NO_SIGN \
  "Format error -- sign missing."
#define CNV_ER_FMT_BAD_LEADING \
  "Format error -- invalid leading `%s'."
#define CNV_ER_FMT_BAD_TRAILING \
  "Format error -- invalid trailing `%s'."
#define CNV_ER_FMT_BAD_THOUS \
  "Format error -- missing or misplaced thousands separator."
#define CNV_ER_FMT_EXTRA_INTEGER \
  "Format error -- too many digits in integer part."
#define CNV_ER_FMT_EXTRA_FRACTION \
  "Format error -- too many digits in fraction part."
#define CNV_ER_FMT_MISSING_INTEGER \
  "Format error -- not enough digits in integer part."
#define CNV_ER_FMT_MISSING_FRACTION \
  "Format error -- not enough digits in fraction part."
#define CNV_ER_FMT_EMPTY_STRING \
  "Format error -- empty value string."
#define CNV_ER_FMT_BAD_POSITION \
  "Format error -- `%s' in wrong position."
#define CNV_ER_FMT_BAD_NULL \
  "Can't convert non-empty string to DB_TYPE_NULL."
#define CNV_ER_FMT_EXTRA_SIGN \
  "Format error -- too many sign characters."
#define CNV_ER_FMT_INTEGER_OVERFLOW \
  "Invalid integer -- greater than %ld."
#define CNV_ER_FMT_INTEGER_UNDERFLOW \
  "Invalid integer -- less than %ld."
#define CNV_ER_FMT_BAD_PATTERN \
  "Format error -- expected `%s' at position %d."
#define CNV_ER_FMT_BAD_X_DIGITS \
  "Format error -- expected %d digits at position %d."
#define CNV_ER_FMT_NO_CURRENCY \
  "Format error -- currency symbol missing."
#define CNV_ER_FMT_NOT_UNIQUE \
  "Invalid format -- may not describe a unique date or time."
#define CNV_ER_FMT_BAD_DATE \
  "Format error -- missing or invalid date (%%%s)."
#define CNV_ER_FMT_BAD_YEAR \
  "Format error -- missing or invalid year (%%%s)."
#define CNV_ER_FMT_BAD_MONTH \
  "Format error -- missing or invalid month (%%%s)."
#define CNV_ER_FMT_BAD_MDAY \
  "Format error -- missing or invalid month day (%%%s)."
#define CNV_ER_FMT_BAD_WDAY \
  "Format error -- missing or invalid week day (%%%s)."
#define CNV_ER_FMT_FLOAT_OVERFLOW \
  "Invalid float -- greater than %e."
#define CNV_ER_FMT_FLOAT_UNDERFLOW \
  "Invalid float -- less than %e."
#define CNV_ER_FMT_UNKNOWN_DATE \
  "%s does not represent an actual date."
#define CNV_ER_FMT_BAD_TIME \
  "Format error -- missing or invalid time (%%%s)."
#define CNV_ER_FMT_BAD_HOUR \
  "Format error -- missing or invalid hour (%%%s)."
#define CNV_ER_FMT_BAD_MIN \
  "Format error -- missing or invalid minute (%%%s)."
#define CNV_ER_FMT_BAD_SEC \
  "Format error -- missing or invalid second (%%%s)."
#define CNV_ER_FMT_BAD_AM_PM \
  "Format error -- missing or invalid AM/PM (%%%s)."
#define CNV_ER_FMT_BAD_TIMESTAMP \
  "Format error -- missing or invalid timestamp (%%%s)."
#define CNV_ER_FMT_TIMESTAMP_UNDERFLOW \
  "Invalid timestamp -- less than %s."
#define CNV_ER_FMT_TIMESTAMP_OVERFLOW \
  "Invalid timestamp -- greater than %s."
#define CNV_ER_FMT_BAD_FORMAT \
  "`%s' is not a valid %s format."

#endif /* _CNVERR_H_ */
