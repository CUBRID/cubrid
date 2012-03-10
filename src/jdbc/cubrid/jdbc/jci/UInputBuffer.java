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

import java.io.IOException;
import java.sql.Date;
import java.sql.Time;
import java.util.Calendar;

import cubrid.jdbc.driver.CUBRIDBlob;
import cubrid.jdbc.driver.CUBRIDClob;
import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDEnum;
import cubrid.jdbc.driver.CUBRIDXid;
import cubrid.sql.CUBRIDOID;
import cubrid.sql.CUBRIDTimestamp;

class UInputBuffer {
	private UTimedDataInputStream input;
	private int position;
	private int capacity;
	private byte casinfo[];
	private byte buffer[];
	private int resCode;
	private final static int CAS_INFO_SIZE = 4;
	private UConnection uconn;

	UInputBuffer(UTimedDataInputStream relatedI, UConnection con)
			throws IOException, UJciException {
		input = relatedI;
		position = 0;
		uconn = con;

		int readLen = 0;
		int totalReadLen = 0;
		byte[] headerData = new byte[8];

		while (totalReadLen < 8) {
			readLen = input.read(headerData, totalReadLen, 8 - totalReadLen);
			if (readLen == -1) {
				throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
			}
			totalReadLen = totalReadLen + readLen;
		}

		capacity = UJCIUtil.bytes2int(headerData, 0);

		if (capacity < 0) {
			capacity = 0;
			return;
		}

		casinfo = new byte[CAS_INFO_SIZE];
		System.arraycopy(headerData, 4, casinfo, 0, 4);

		buffer = new byte[capacity];
		readData();

		resCode = readInt();
		con.setCASInfo(casinfo);

		if (resCode < 0) {
			int eCode = readInt();
			String msg = readString(remainedCapacity(),
					UJCIManager.sysCharsetName);
			throw uconn.createJciException(UErrorCode.ER_DBMS, resCode, eCode, msg);
		}
	}

	byte[] getCasInfo() {
		return casinfo;
	}

	int getResCode() {
		return resCode;
	}

	byte readByte() throws UJciException {
		if (position >= capacity) {
			throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}

		return buffer[position++];
	}

	void readBytes(byte value[], int offset, int len) throws UJciException {
		if (value == null)
			return;

		if (position + len > capacity) {
		    throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}

		System.arraycopy(buffer, position, value, offset, len);
		position += len;
	}

	void readBytes(byte value[]) throws UJciException {
		readBytes(value, 0, value.length);
	}

	byte[] readBytes(int size) throws UJciException {
		byte[] value = new byte[size];
		readBytes(value, 0, size);
		return value;
	}

	double readDouble() throws UJciException {
		return Double.longBitsToDouble(readLong());
	}

	float readFloat() throws UJciException {
		return Float.intBitsToFloat(readInt());
	}

	int readInt() throws UJciException {
		if (position + 4 > capacity) {
			throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}

		int data = UJCIUtil.bytes2int(buffer, position);
		position += 4;

		return data;
	}

	long readLong() throws UJciException {
		long data = 0;

		if (position + 8 > capacity) {
		    	throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}

		for (int i = 0; i < 8; i++) {
			data <<= 8;
			data |= (buffer[position++] & 0xff);
		}

		return data;
	}

	short readShort() throws UJciException {
		if (position + 2 > capacity) {
		    	throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}

		short data = UJCIUtil.bytes2short(buffer, position);
		position += 2;

		return data;
	}

	String readString(int size, String charsetName) throws UJciException {
		String stringData;

		if (size <= 0)
			return null;

		if (position + size > capacity) {
		    	throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}

		try {
			stringData = new String(buffer, position, size - 1, charsetName);
		} catch (java.io.UnsupportedEncodingException e) {
			stringData = new String(buffer, position, size - 1);
		}

		position += size;

		return stringData;
	}

	Date readDate() throws UJciException {
		int year, month, day;
		year = readShort();
		month = readShort();
		day = readShort();

		if (year == 0 && month == 0 && day == 0) {
			if (uconn.getZeroDateTimeBehavior().equals(
					UConnection.ZERO_DATETIME_BEHAVIOR_EXCEPTION)) {
				throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
			} else if (uconn.getZeroDateTimeBehavior().equals(
					UConnection.ZERO_DATETIME_BEHAVIOR_CONVERT_TO_NULL)) {
				return null;
			}
		}

		Calendar cal = Calendar.getInstance();
		if (year == 0 && month == 0 && day == 0) {
			cal.set(0, 0, 1, 0, 0, 0); /* round to 0001-01-01 00:00:00) */
		} else {
			cal.set(year, month - 1, day, 0, 0, 0);
		}

		return new Date(cal.getTimeInMillis());
	}

	Time readTime() throws UJciException {
		int hour, minute, second;
		hour = readShort();
		minute = readShort();
		second = readShort();

		Calendar cal = Calendar.getInstance();
		cal.set(1970, 0, 1, hour, minute, second);

		return new Time(cal.getTimeInMillis());
	}

	CUBRIDTimestamp readTimestamp() throws UJciException {
		int year, month, day, hour, minute, second;
		year = readShort();
		month = readShort();
		day = readShort();
		hour = readShort();
		minute = readShort();
		second = readShort();

		if (year == 0 && month == 0 && day == 0) {
			if (uconn.getZeroDateTimeBehavior().equals(
					UConnection.ZERO_DATETIME_BEHAVIOR_EXCEPTION)) {
				throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
			} else if (uconn.getZeroDateTimeBehavior().equals(
					UConnection.ZERO_DATETIME_BEHAVIOR_CONVERT_TO_NULL)) {
				return null;
			}
		}

		Calendar cal = Calendar.getInstance();
		if (year == 0 && month == 0 && day == 0) {
			cal.set(0, 0, 1, 0, 0, 0); /* round to 0001-01-01 00:00:00) */
		} else {
			cal.set(year, month - 1, day, hour, minute, second);
		}
		cal.set(Calendar.MILLISECOND, 0);

		return new CUBRIDTimestamp(cal.getTimeInMillis(),
				CUBRIDTimestamp.TIMESTAMP);
	}

	CUBRIDTimestamp readDatetime() throws UJciException {
		int year, month, day, hour, minute, second, millisecond;
		year = readShort();
		month = readShort();
		day = readShort();
		hour = readShort();
		minute = readShort();
		second = readShort();
		millisecond = readShort();

		if (year == 0 && month == 0 && day == 0) {
			if (uconn.getZeroDateTimeBehavior().equals(
					UConnection.ZERO_DATETIME_BEHAVIOR_EXCEPTION)) {
				throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
			} else if (uconn.getZeroDateTimeBehavior().equals(
					UConnection.ZERO_DATETIME_BEHAVIOR_CONVERT_TO_NULL)) {
				return null;
			}
		}

		Calendar cal = Calendar.getInstance();
		if (year == 0 && month == 0 && day == 0) {
			cal.set(0, 0, 1, 0, 0, 0); /* round to 0001-01-01 00:00:00) */
		} else {
			cal.set(year, month - 1, day, hour, minute, second);
		}
		cal.set(Calendar.MILLISECOND, millisecond);

		return new CUBRIDTimestamp(cal.getTimeInMillis(),
				CUBRIDTimestamp.DATETIME);
	}

	CUBRIDOID readOID(CUBRIDConnection con) throws UJciException {
		byte[] oid = readBytes(UConnection.OID_BYTE_SIZE);
		for (int i = 0; i < oid.length; i++) {
			if (oid[i] != (byte) 0) {
				return (new CUBRIDOID(con, oid));
			}
		}
		return null;
	}

	CUBRIDEnum readEnum(CUBRIDConnection con) throws UJciException {
		short val = readShort();
		return new CUBRIDEnum(con, val);
	}

	CUBRIDBlob readBlob(int packedLobHandleSize, CUBRIDConnection conn)
			throws UJciException {
		try {
			byte[] packedLobHandle = readBytes(packedLobHandleSize);
			return new CUBRIDBlob(conn, packedLobHandle);
		} catch (Exception e) {
		    	throw uconn.createJciException(UErrorCode.ER_UNKNOWN);
		}
	}

	CUBRIDClob readClob(int packedLobHandleSize, CUBRIDConnection conn)
			throws UJciException {
		try {
			byte[] packedLobHandle = readBytes(packedLobHandleSize);
			return new CUBRIDClob(conn, packedLobHandle, conn.getUConnection()
					.getCharset());
		} catch (Exception e) {
		    	throw uconn.createJciException(UErrorCode.ER_UNKNOWN);
		}
	}

	int remainedCapacity() {
		return capacity - position;
	}

	CUBRIDXid readXid() throws UJciException {
		readInt(); // msg_size
		int formatId = readInt();
		int gid_size = readInt();
		int bid_size = readInt();
		byte[] gid = readBytes(gid_size);
		byte[] bid = readBytes(bid_size);

		return (new CUBRIDXid(formatId, gid, bid));
	}

	private void readData() throws IOException {
		int realRead = 0, tempRead = 0;
		while (realRead < capacity) {
			tempRead = input.read(buffer, realRead, capacity - realRead);
			if (tempRead < 0) {
				capacity = realRead;
				break;
			}
			realRead += tempRead;
		}
	}
}
