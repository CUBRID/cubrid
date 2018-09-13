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

/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;

import cubrid.jdbc.driver.CUBRIDBlob;
import cubrid.jdbc.driver.CUBRIDClob;
import cubrid.jdbc.driver.CUBRIDBinaryString;
import cubrid.sql.CUBRIDOID;
import cubrid.sql.CUBRIDTimestamp;
import cubrid.sql.CUBRIDTimestamptz;

/**
 * CUBRID Data Type을 정의해 놓은 class이다.
 * 
 * since 1.0
 */

abstract public class UUType {
	public static final int U_TYPE_MIN = 0;
	public static final int U_TYPE_MAX = 32;

	public static final byte U_TYPE_NULL = 0;
	public static final byte U_TYPE_CHAR = 1;
	public static final byte U_TYPE_STRING = 2;
	public static final byte U_TYPE_VARCHAR = 2;
	public static final byte U_TYPE_NCHAR = 3;
	public static final byte U_TYPE_VARNCHAR = 4;
	public static final byte U_TYPE_BIT = 5;
	public static final byte U_TYPE_VARBIT = 6;
	public static final byte U_TYPE_NUMERIC = 7;
	public static final byte U_TYPE_DECIMAL = 7;
	public static final byte U_TYPE_INT = 8;
	public static final byte U_TYPE_SHORT = 9;
	public static final byte U_TYPE_MONETARY = 10;
	public static final byte U_TYPE_FLOAT = 11;
	public static final byte U_TYPE_DOUBLE = 12;
	public static final byte U_TYPE_DATE = 13;
	public static final byte U_TYPE_TIME = 14;
	public static final byte U_TYPE_TIMESTAMP = 15;
	public static final byte U_TYPE_SET = 16;
	public static final byte U_TYPE_MULTISET = 17;
	public static final byte U_TYPE_SEQUENCE = 18;
	public static final byte U_TYPE_OBJECT = 19;
	public static final byte U_TYPE_RESULTSET = 20;
	public static final byte U_TYPE_BIGINT = 21;
	public static final byte U_TYPE_DATETIME = 22;
	public static final byte U_TYPE_BLOB = 23;
	public static final byte U_TYPE_CLOB = 24;
	public static final byte U_TYPE_ENUM = 25;
	public static final byte U_TYPE_USHORT = 26;
	public static final byte U_TYPE_UINT = 27;
	public static final byte U_TYPE_UBIGINT = 28;
	public static final byte U_TYPE_TIMESTAMPTZ = 29;
	public static final byte U_TYPE_TIMESTAMPLTZ = 30;
	public static final byte U_TYPE_DATETIMETZ = 31;
	public static final byte U_TYPE_DATETIMELTZ = 32;
	public static final byte U_TYPE_TIMETZ = 33;
	
	static boolean isCollectionType(byte type) {
		if (type == UUType.U_TYPE_SET || type == UUType.U_TYPE_MULTISET
				|| type == UUType.U_TYPE_SEQUENCE) {
			return true;
		}
		return false;
	}

	static byte getObjArrBaseDBtype(Object values) {
		if (values instanceof String[])
			return UUType.U_TYPE_VARCHAR;
		else if (values instanceof Byte[])
			return UUType.U_TYPE_SHORT;
		else if (values instanceof byte[][])
			return UUType.U_TYPE_VARBIT;
		else if (values instanceof Boolean[])
			return UUType.U_TYPE_BIT;
		else if (values instanceof Short[])
			return UUType.U_TYPE_SHORT;
		else if (values instanceof Integer[])
			return UUType.U_TYPE_INT;
		else if (values instanceof Long[])
			return UUType.U_TYPE_BIGINT;
		else if (values instanceof Double[])
			return UUType.U_TYPE_DOUBLE;
		else if (values instanceof Float[])
			return UUType.U_TYPE_FLOAT;
		else if (values instanceof BigDecimal[])
			return UUType.U_TYPE_NUMERIC;
		else if (values instanceof Date[])
			return UUType.U_TYPE_DATE;
		else if (values instanceof Time[])
			return UUType.U_TYPE_TIME;
		else if (values instanceof Timestamp[]) {
			for (int i = 0; i < ((Object[]) values).length; i++) {
				if (!CUBRIDTimestamp
						.isTimestampType((Timestamp) (((Object[]) values)[i]))) {
					return UUType.U_TYPE_DATETIME;
				}
			}
			return UUType.U_TYPE_TIMESTAMP;
		} else if (values instanceof CUBRIDOID[])
			return UUType.U_TYPE_OBJECT;
		else if (values instanceof CUBRIDBlob[])
			return UUType.U_TYPE_BLOB;
		else if (values instanceof CUBRIDClob[])
			return UUType.U_TYPE_CLOB;
		else
			return UUType.U_TYPE_NULL;
	}

	static byte getObjectDBtype(Object value) {
		if (value == null)
			return UUType.U_TYPE_NULL;
		else if (value instanceof String)
			return UUType.U_TYPE_STRING;
		else if (value instanceof Byte)
			return UUType.U_TYPE_SHORT;
		else if (value instanceof byte[])
			return UUType.U_TYPE_VARBIT;
		else if (value instanceof Boolean)
			return UUType.U_TYPE_BIT;
		else if (value instanceof Short)
			return UUType.U_TYPE_SHORT;
		else if (value instanceof Integer)
			return UUType.U_TYPE_INT;
		else if (value instanceof Long)
			return UUType.U_TYPE_BIGINT;
		else if (value instanceof Double)
			return UUType.U_TYPE_DOUBLE;
		else if (value instanceof Float)
			return UUType.U_TYPE_FLOAT;
		else if (value instanceof BigDecimal || value instanceof Long)
			return UUType.U_TYPE_NUMERIC;
		else if (value instanceof Date)
			return UUType.U_TYPE_DATE;
		else if (value instanceof Time)
			return UUType.U_TYPE_TIME;
		else if (value instanceof CUBRIDTimestamptz) {
			if (CUBRIDTimestamp.isTimestampType((Timestamp) value)) {
				return UUType.U_TYPE_TIMESTAMPTZ;
			}
			return UUType.U_TYPE_DATETIMETZ;
		} else if (value instanceof Timestamp) {
			if (CUBRIDTimestamp.isTimestampType((Timestamp) value)) {
				return UUType.U_TYPE_TIMESTAMP;
			}
			return UUType.U_TYPE_DATETIME;
		} else if (value instanceof CUBRIDOID)
			return UUType.U_TYPE_OBJECT;
		else if (value instanceof CUBRIDBlob)
			return UUType.U_TYPE_BLOB;
		else if (value instanceof CUBRIDClob)
			return UUType.U_TYPE_CLOB;
		else if (value instanceof Object[])
			return UUType.U_TYPE_SEQUENCE;
		else if (value instanceof CUBRIDBinaryString)
			return UUType.U_TYPE_VARCHAR;
		else
			return UUType.U_TYPE_NULL;
	}
}
