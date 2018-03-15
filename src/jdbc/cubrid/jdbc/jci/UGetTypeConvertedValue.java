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
import cubrid.sql.CUBRIDTimestamp;
import cubrid.sql.CUBRIDTimestamptz;
import cubrid.jdbc.driver.CUBRIDBinaryString;
import cubrid.jdbc.driver.CUBRIDException;

abstract public class UGetTypeConvertedValue {

	static public BigDecimal getBigDecimal(Object data) throws UJciException {
		if (data == null)
			return null;
		else if (data instanceof BigDecimal)
			return (BigDecimal) data;
		else if (data instanceof String) {
			try {
				return new BigDecimal((String) data);
			} catch (NumberFormatException e) {
				throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
			}
		} else if (data instanceof Long) {
			return new BigDecimal(((Long) data).longValue());
		} else if (data instanceof Number) {
			return new BigDecimal(((Number) data).doubleValue());
		}
		else if (data instanceof Boolean)
			return new BigDecimal(
					(((Boolean) data).booleanValue() == true) ? (double) 1
							: (double) 0);
		throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
	}

	static public boolean getBoolean(Object data) throws UJciException {
		if (data == null)
			return false;
		else if (data instanceof Boolean)
			return ((Boolean) data).booleanValue();
		else if (data instanceof String)
			return (((((String) data).trim()).compareTo("0") == 0) ? false : true);
		else if (data instanceof Number)
			return ((((Number) data).doubleValue() == (double) 0) ? false
					: true);
		else if (data instanceof byte[])
			return ((((byte[]) data)[0] == 0) ? false : true);
		throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
	}

	static public byte getByte(Object data) throws UJciException {
		if (data == null)
			return (byte) 0;
		else if (data instanceof Number)
			return ((Number) data).byteValue();
		else if (data instanceof byte[]) {
			if (((byte[]) data).length != 1)
				throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
			return ((byte[]) data)[0];
		} else if (data instanceof String) {
			try {
				return Byte.parseByte((String) data);
			} catch (NumberFormatException e) {
				throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
			}
		} else if (data instanceof Boolean)
			return ((((Boolean) data).booleanValue() == true) ? (byte) -128
					: (byte) 0);
		throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
	}

	static public byte[] getBytes(Object data) throws UJciException {
		if (data == null)
			return null;

		if (data instanceof byte[])
			return (byte[]) ((byte[]) data).clone();

		throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
	}

	static public Date getDate(Object data) throws UJciException {
		if (data == null)
			return null;

		else if (data instanceof Date)
			return new Date(((Date) data).getTime());
		else if (data instanceof Timestamp)
			return new Date(((Timestamp) data).getTime());
		else if (data instanceof String) {
			try {
				return Date.valueOf((String) data);
			} catch (IllegalArgumentException e) {
				throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
			}
		}

		throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
	}

	static public double getDouble(Object data) throws UJciException {
		if (data == null)
			return (double) 0;
		else if (data instanceof Number)
			return ((Number) data).doubleValue();
		else if (data instanceof String) {
			try {
				return Double.parseDouble((String) data);
			} catch (NumberFormatException e) {
				throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
			}
		} else if (data instanceof Boolean)
			return ((((Boolean) data).booleanValue() == true) ? (double) 1
					: (double) 0);
		throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
	}

	static public float getFloat(Object data) throws UJciException {
		if (data == null)
			return (float) 0;
		else if (data instanceof Number)
			return ((Number) data).floatValue();
		else if (data instanceof String) {
			try {
				return Float.parseFloat((String) data);
			} catch (NumberFormatException e) {
				throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
			}
		} else if (data instanceof Boolean)
			return ((((Boolean) data).booleanValue() == true) ? (float) 1
					: (float) 0);
		throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
	}

	static public int getInt(Object data) throws UJciException {
		if (data == null)
			return 0;
		else if (data instanceof Number)
			return ((Number) data).intValue();
		else if (data instanceof String) {
			try {
				return Integer.parseInt((String) data);
			} catch (NumberFormatException e) {
				throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
			}
		} else if (data instanceof Boolean)
			return ((((Boolean) data).booleanValue() == true) ? 1 : 0);
		throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
	}

	static public long getLong(Object data) throws UJciException {
		if (data == null)
			return (long) 0;
		else if (data instanceof String) {
			try {
				return Long.parseLong((String) data);
			} catch (NumberFormatException e) {
				throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
			}
		} else if (data instanceof Number)
			return ((Number) data).longValue();
		else if (data instanceof Boolean)
			return ((((Boolean) data).booleanValue() == true) ? (long) 1
					: (long) 0);
		throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
	}

	static public short getShort(Object data) throws UJciException {
		if (data == null)
			return (short) 0;
		else if (data instanceof Number)
			return ((Number) data).shortValue();
		else if (data instanceof String) {
			try {
				return Short.parseShort((String) data);
			} catch (NumberFormatException e) {
				throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
			}
		} else if (data instanceof Boolean)
			return ((((Boolean) data).booleanValue() == true) ? (short) 1
					: (short) 0);
		throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
	}

    static public String getString(Object data) throws UJciException {
	if (data == null) {
	    return null;
	} else if (data instanceof String) {
	    return ((String) data);
	} else if (data instanceof BigDecimal) {
	    return ((BigDecimal) data).toPlainString();
	} else if ((data instanceof Number) || (data instanceof Boolean)
		|| (data instanceof Date) || (data instanceof Time)
		|| (data instanceof CUBRIDTimestamptz)) {
	    return data.toString();
	} else if (data instanceof Timestamp) {
	    String form;

	    if (CUBRIDTimestamp.isTimestampType((Timestamp) data)) {
		form = "yyyy-MM-dd HH:mm:ss";
	    } else {
		form = "yyyy-MM-dd HH:mm:ss.SSS";
	    }

	    java.text.SimpleDateFormat f = new java.text.SimpleDateFormat(form);

	    return f.format(data);
	} else if (data instanceof CUBRIDOID) {
	    try {
		return ((CUBRIDOID) data).getOidString();
	    } catch (Exception e) {
		return "";
	    }
	} else if (data instanceof byte[]) {
	    return UGetTypeConvertedValue.getHexaDecimalString((byte[]) data);
	} else if ((data instanceof Blob) || (data instanceof Clob)) {
	    return data.toString();
	} else if (data instanceof CUBRIDBinaryString) {
	    return (((CUBRIDBinaryString) data).toString());
	}
	throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
    }

	static public Time getTime(Object data) throws UJciException {
		if (data == null)
			return null;
		else if (data instanceof Time)
			return new Time(((Time) data).getTime());
		else if (data instanceof String) {
			try {
				return Time.valueOf((String) data);
			} catch (IllegalArgumentException e) {
				throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
			}
		} else if (data instanceof Timestamp)
			return new Time(((Timestamp) data).getTime());
		throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
	}
	
	static public Timestamp getTimestamp(Object data) throws UJciException {
		if (data == null)
			return null;
		else if (data instanceof Timestamp)
			return new Timestamp(((Timestamp) data).getTime());
		else if (data instanceof String) {
			try {
				return Timestamp.valueOf((String) data);
			} catch (IllegalArgumentException e) {
				throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
			}
		} else if (data instanceof Date)
			return new Timestamp(((Date) data).getTime());
		else if (data instanceof Time)
			return new Timestamp(((Time) data).getTime());
		throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
	}
	
	static public CUBRIDTimestamptz getTimestamptz(Object data) throws UJciException {
		if (data == null)
			return null;
		else if (data instanceof CUBRIDTimestamptz)
			return new CUBRIDTimestamptz(((CUBRIDTimestamptz) data).getTime(),
										!CUBRIDTimestamp.isTimestampType ((CUBRIDTimestamp) data),
										((CUBRIDTimestamptz) data).getTimezone());
		else if (data instanceof String) {
			try {
				return new CUBRIDTimestamptz((String) data);
			} catch (CUBRIDException e) {
				throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
			}
		}
		else if (data instanceof Time)
			return new CUBRIDTimestamptz(((Time) data).getTime(), false, "");
		else if (data instanceof Timestamp)
			return new CUBRIDTimestamptz(((Timestamp) data).getTime(), false, "");
		throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
	}

	static private String getHexaDecimalString(byte[] data) {
		String stringData = "", aByteString;
		int temp = 0, halfByte;
		final short aByteSize = 256;

		for (int i = 0; i < data.length; i++) {
			if (data[i] < 0)
				temp = (short) data[i] + aByteSize;
			else
				temp = (short) data[i];
			aByteString = "";
			for (int j = 0; j < 2; j++) {
				halfByte = temp % 16;
				aByteString = ((halfByte < 10) ? String.valueOf(halfByte)
						: ((halfByte == 10) ? "a"
								: ((halfByte == 11) ? "b"
										: ((halfByte == 12) ? "c"
												: ((halfByte == 13) ? "d"
														: ((halfByte == 14) ? "e"
																: "f"))))))
						+ aByteString;
				temp /= 16;
			}
			stringData += aByteString;
		}
		return stringData;
	}

	/*
	 * static private String getBinaryDecimalString(int precision, byte[] data)
	 * { String stringData="", aByteString=""; final short aByteSize = 256; int
	 * temp;
	 * 
	 * for(int i=0 ; i< data.length ; i++){ if (data[i] < 0) temp = (short)
	 * data[i] + aByteSize; else temp = (short) data[i]; aByteString=""; for(int
	 * j=0 ; j<8 ; j++){ aByteString = String.valueOf(temp%2) + aByteString;
	 * temp /= 2; } stringData += aByteString; } return stringData.substring(0,
	 * precision); }
	 */
}
