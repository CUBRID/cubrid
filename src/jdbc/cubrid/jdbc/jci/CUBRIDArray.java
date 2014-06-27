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
import java.sql.Blob;
import java.sql.Clob;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;

import cubrid.sql.CUBRIDOID;

class CUBRIDArray {
	private byte baseType;
	private int length;
	private Object internalArray[];

	CUBRIDArray(byte type, int arrayLength) throws UJciException {
		baseType = type;
		length = arrayLength;
		if (length < 0)
			return;

		switch (type) {
		case UUType.U_TYPE_BIT:
		case UUType.U_TYPE_VARBIT:
			internalArray = (Object[]) (new byte[length][]);
			break;
		case UUType.U_TYPE_SHORT:
		case UUType.U_TYPE_USHORT:
			internalArray = (Object[]) (new Short[length]);
			break;
		case UUType.U_TYPE_INT:
		case UUType.U_TYPE_UINT:
			internalArray = (Object[]) (new Integer[length]);
			break;
		case UUType.U_TYPE_BIGINT:
		case UUType.U_TYPE_UBIGINT:
			internalArray = (Object[]) (new Long[length]);
			break;
		case UUType.U_TYPE_FLOAT:
			internalArray = (Object[]) (new Float[length]);
			break;
		case UUType.U_TYPE_DOUBLE:
		case UUType.U_TYPE_MONETARY:
			internalArray = (Object[]) (new Double[length]);
			break;
		case UUType.U_TYPE_NUMERIC:
			internalArray = (Object[]) (new BigDecimal[length]);
			break;
		case UUType.U_TYPE_DATE:
			internalArray = (Object[]) (new Date[length]);
			break;
		case UUType.U_TYPE_TIME:
			internalArray = (Object[]) (new Time[length]);
			break;
		case UUType.U_TYPE_TIMESTAMP:
		case UUType.U_TYPE_DATETIME:
			internalArray = (Object[]) (new Timestamp[length]);
			break;
		case UUType.U_TYPE_CHAR:
		case UUType.U_TYPE_NCHAR:
		case UUType.U_TYPE_STRING:
		case UUType.U_TYPE_VARNCHAR:
		case UUType.U_TYPE_ENUM:
			internalArray = (Object[]) (new String[length]);
			break;
		case UUType.U_TYPE_OBJECT:
			internalArray = (Object[]) (new CUBRIDOID[length]);
			break;
		case UUType.U_TYPE_BLOB:
			internalArray = (Object[]) (new Blob[length]);
			break;
		case UUType.U_TYPE_CLOB:
			internalArray = (Object[]) (new Clob[length]);
			break;
		case UUType.U_TYPE_NULL:
		case UUType.U_TYPE_SET:
		case UUType.U_TYPE_MULTISET:
		case UUType.U_TYPE_SEQUENCE:
		case UUType.U_TYPE_RESULTSET:	
			baseType = UUType.U_TYPE_NULL;
			internalArray = new Object[length];
			break;
		default:
			throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
		}
	}

	CUBRIDArray(Object values) throws UJciException {
		if ((values instanceof Object[]) && (((Object[]) values).length == 0))
			baseType = UUType.U_TYPE_OBJECT;
		else
			baseType = UUType.getObjArrBaseDBtype(values);
		if (baseType == UUType.U_TYPE_NULL)
			throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
		internalArray = (Object[]) ((Object[]) values).clone();
		length = ((Object[]) values).length;
	}

	Object getArray() {
		return internalArray;
	}

	Object getArrayClone() {
		if (internalArray == null)
			return null;

		Object[] obj = (Object[]) internalArray.clone();

		if (obj instanceof Date[]) {
			for (int i = 0; i < obj.length; i++)
				if (obj[i] != null)
					obj[i] = ((Date) obj[i]).clone();
		} else if (obj instanceof Time[]) {
			for (int i = 0; i < obj.length; i++)
				if (obj[i] != null)
					obj[i] = ((Time) obj[i]).clone();
		} else if (obj instanceof Timestamp[]) {
			for (int i = 0; i < obj.length; i++)
				if (obj[i] != null)
					obj[i] = ((Timestamp) obj[i]).clone();
		} else if (obj instanceof byte[][]) {
			for (int i = 0; i < obj.length; i++)
				if (obj[i] != null)
					obj[i] = ((byte[]) obj[i]).clone();
		}

		return obj;
	}

	int getBaseType() {
		return (int) baseType;
	}

	int getLength() {
		return length;
	}

	synchronized void setElement(int index, Object data) {
		internalArray[index] = data;
	}
}
