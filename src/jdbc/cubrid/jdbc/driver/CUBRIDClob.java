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

package cubrid.jdbc.driver;

import java.io.Flushable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.Reader;
import java.io.UnsupportedEncodingException;
import java.io.Writer;
import java.sql.Clob;
import java.sql.SQLException;
import java.util.ArrayList;

import cubrid.jdbc.jci.UUType;

public class CUBRIDClob implements Clob {

	/*
	 * ======================================================================= |
	 * CONSTANT VALUES
	 * =======================================================================
	 */
	private final static int CLOB_MAX_IO_LENGTH = 128 * 1024; // 128kB at once
	private final static int CLOB_MAX_IO_CHARS = CLOB_MAX_IO_LENGTH / 2;

	/*
	 * ======================================================================= |
	 * PRIVATE
	 * =======================================================================
	 */
	private CUBRIDConnection conn;
	private boolean isWritable;
	private CUBRIDLobHandle lobHandle;
	private String charsetName;

	private StringBuffer clobCharBuffer = new StringBuffer("");
	private long clobCharPos;
	private long clobCharLength;

	private byte[] clobByteBuffer = new byte[CLOB_MAX_IO_LENGTH];
	private long clobBytePos;
	private long clobNextReadBytePos;
	// no 'clobByteLength' member: USE 'lobHandle.getLobSize()'

	private ArrayList<java.io.Flushable> streamList = new ArrayList<java.io.Flushable>();

	/*
	 * ======================================================================= |
	 * CONSTRUCTOR
	 * =======================================================================
	 */
	// make a new clob
	public CUBRIDClob(CUBRIDConnection conn, String charsetName)
			throws SQLException {
		if (conn == null) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_value);
		}

		byte[] packedLobHandle = conn.lobNew(UUType.U_TYPE_CLOB);

		this.conn = conn;
		isWritable = true;
		lobHandle = new CUBRIDLobHandle(UUType.U_TYPE_CLOB, packedLobHandle);
		this.charsetName = charsetName;

		clobCharPos = 0;
		clobCharLength = 0;
		clobBytePos = 0;
		clobNextReadBytePos = 0;
	}

	// get clob from existing result set
	public CUBRIDClob(CUBRIDConnection conn, byte[] packedLobHandle,
			String charsetName) throws SQLException {
		if (conn == null || packedLobHandle == null) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_value);
		}

		this.conn = conn;
		isWritable = false;
		lobHandle = new CUBRIDLobHandle(UUType.U_TYPE_CLOB, packedLobHandle);
		this.charsetName = charsetName;

		clobCharPos = 0;
		clobCharLength = -1;
		clobBytePos = 0;
		clobNextReadBytePos = 0;
	}

	/*
	 * ======================================================================= |
	 * java.sql.Clob interface
	 * =======================================================================
	 */
	public synchronized long length() throws SQLException {
		if (lobHandle == null) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (clobCharLength < 0) {
			readClobPartially(Long.MAX_VALUE, 1);
		}

		return clobCharLength;
	}

	public synchronized String getSubString(long pos, int length)
			throws SQLException {
		if (lobHandle == null) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (pos < 1 || length < 0) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (length == 0) {
			return "";
		}

		int read_len = readClobPartially(pos, length);
		if (read_len <= 0) {
			return "";
		}

		return (clobCharBuffer.substring(0, read_len));
	}

	public Reader getCharacterStream() throws SQLException {
		return getCharacterStream(1, Long.MAX_VALUE);
	}

	/* JDK 1.6 */
	public Reader getCharacterStream(long pos, long length) throws SQLException {
		if (lobHandle == null) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (pos < 1 || length < 0) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}

		return new CUBRIDBufferedReader(
				new CUBRIDClobReader(this, pos, length), CLOB_MAX_IO_CHARS);
	}

	public InputStream getAsciiStream() throws SQLException {
		if (lobHandle == null) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}

		return new CUBRIDBufferedInputStream(new CUBRIDClobInputStream(this),
				CLOB_MAX_IO_LENGTH);
	}

	public long position(String searchstr, long start) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	public long position(Clob searchClob, long start) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	public synchronized int setString(long pos, String str) throws SQLException {
		if (lobHandle == null) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (pos < 1) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (!isWritable) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.lob_is_not_writable, null);
		}
		if (str == null || str.length() <= 0) {
			return 0;
		}

		if (readClobPartially(pos, 1) != 0) {
			// only append is allowed.
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.lob_pos_invalid, null);
		}

		byte[] bytes = string2bytes(str);
		int bytes_len = bytes.length;
		int bytes_offset = 0;

		while (bytes_len > 0) {
			int bytesWritten = conn.lobWrite(lobHandle.getPackedLobHandle(),
					clobBytePos + bytes_offset, bytes, bytes_offset, Math.min(
							bytes_len, CLOB_MAX_IO_LENGTH));

			bytes_len -= bytesWritten;
			bytes_offset += bytesWritten;
		}

		lobHandle.setLobSize(clobBytePos + bytes_offset);
		clobCharLength = length() + str.length();

		return str.length();
	}

	public int setString(long pos, String str, int offset, int len)
			throws SQLException {
		if (lobHandle == null) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (pos < 1 || offset < 0 || len < 0) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (offset + len > str.length()) {
			throw new IndexOutOfBoundsException();
		}
		if (!isWritable) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.lob_is_not_writable, null);
		}
		if (str == null || len == 0) {
			return 0;
		}

		return (setString(pos, str.substring(offset, offset + len)));
	}

	public synchronized OutputStream setAsciiStream(long pos)
			throws SQLException {
		if (lobHandle == null) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (pos < 1) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (!isWritable) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.lob_is_not_writable, null);
		}

		if (readClobPartially(pos, 1) != 0) {
			// only append is allowed.
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.lob_pos_invalid, null);
		}

		OutputStream out = new CUBRIDBufferedOutputStream(
				new CUBRIDClobOutputStream(this, clobBytePos + 1),
				CLOB_MAX_IO_LENGTH);
		addFlushableStream(out);
		return out;
	}

	public Writer setCharacterStream(long pos) throws SQLException {
		if (lobHandle == null) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (pos < 1) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (!isWritable) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.lob_is_not_writable, null);
		}

		if (readClobPartially(pos, 1) != 0) {
			// only append is allowed.
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.lob_pos_invalid, null);
		}

		Writer out = new CUBRIDBufferedWriter(new CUBRIDClobWriter(this, pos),
				CLOB_MAX_IO_CHARS);
		addFlushableStream(out);
		return out;
	}

	public void truncate(long len) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void free() throws SQLException {
		conn = null;
		lobHandle = null;
		streamList = null;
		clobCharBuffer = null;
		clobByteBuffer = null;
		isWritable = false;
	}

	private int readClobPartially(long pos, int length) throws SQLException {
		if (clobCharLength != -1 && pos > clobCharLength) {
			clobBytePos = clobNextReadBytePos = lobHandle.getLobSize();
			clobCharPos = clobCharLength;
			clobCharBuffer.setLength(0);
			if (pos == clobCharLength + 1)
				return 0;
			else
				return -1;
		}

		pos--; // pos is now offset from 0

		if (pos < clobCharPos) {
			clobBytePos = clobNextReadBytePos = 0;
			clobCharPos = 0;
			clobCharBuffer.setLength(0);
			readClob();
		}

		while (pos >= clobCharPos + clobCharBuffer.length()) {
			clobBytePos = clobNextReadBytePos;
			clobCharPos += clobCharBuffer.length();
			clobCharBuffer.setLength(0);
			if (clobNextReadBytePos >= lobHandle.getLobSize()) {
				return 0;
			}
			readClob();
		}

		int delete_len = (int) (pos - clobCharPos);
		if (delete_len > 0) {
			clobCharPos = pos;
			clobBytePos += string2bytes(clobCharBuffer.substring(0, delete_len)).length;
			clobCharBuffer.delete(0, delete_len);
		}

		while (length > clobCharBuffer.length()) {
			if (clobNextReadBytePos >= lobHandle.getLobSize()) {
				return clobCharBuffer.length();
			}
			readClob();
		}

		return length;
	}

	private void readClob() throws SQLException {
		if (conn == null || lobHandle == null) {
			throw new NullPointerException();
		}

		int read_len = conn.lobRead(lobHandle.getPackedLobHandle(),
				clobNextReadBytePos, clobByteBuffer, 0, CLOB_MAX_IO_LENGTH);

		StringBuffer sb = new StringBuffer(bytes2string(clobByteBuffer, 0,
				read_len));

		if (clobNextReadBytePos + read_len >= lobHandle.getLobSize())      // End of CLOB
		{
			clobNextReadBytePos += read_len;
			clobCharLength = clobCharPos + clobCharBuffer.length()
					+ sb.length();
		} else {
			clobNextReadBytePos += string2bytes (sb.substring (0, sb.length () - 1)).length;
			sb.setLength(sb.length() - 1);
		}

		clobCharBuffer.append(sb);
	}

	private byte[] string2bytes(String s) throws SQLException {
		try {
			return (s.getBytes(charsetName));
		} catch (UnsupportedEncodingException e) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.unknown, e.getMessage(), e);
		}
	}

	private String bytes2string(byte[] b, int start, int len)
			throws SQLException {
		try {
			return (new String(b, start, len, charsetName));
		} catch (UnsupportedEncodingException e) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.unknown, e.getMessage(), e);
		}
	}

	public CUBRIDLobHandle getLobHandle() {
		return lobHandle;
	}

	private void addFlushableStream(Flushable out) {
		streamList.add(out);
	}

	public void removeFlushableStream(Flushable out) {
		streamList.remove(out);
	}

	public void flushFlushableStreams() {
		if (!streamList.isEmpty()) {
			for (Flushable out : streamList) {
				try {
					out.flush();
				} catch (IOException e) {
				}
			}
		}
	}

	public String toString() {
		return lobHandle.toString();
	}

	public boolean equals(Object obj) {
		if (obj instanceof CUBRIDClob) {
			CUBRIDClob that = (CUBRIDClob) obj;
			return lobHandle.equals(that.lobHandle);
		}
		return false;
	}

	public byte[] getBytes(long pos, int length) throws SQLException {
		if (conn == null || lobHandle == null) {
			throw new NullPointerException();
		}
		if (pos < 1 || length < 0) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (length == 0) {
			return new byte[0];
		}

		pos--; // pos is now offset from 0
		int real_read_len, read_len, total_read_len = 0;

		if (pos + length > lobHandle.getLobSize()) {
			length = (int) (lobHandle.getLobSize() - pos);
		}

		byte[] buf = new byte[length];

		while (length > 0) {
			read_len = Math.min(length, CLOB_MAX_IO_LENGTH);
			real_read_len = conn.lobRead(lobHandle.getPackedLobHandle(), pos,
					buf, total_read_len, read_len);

			pos += real_read_len;
			length -= real_read_len;
			total_read_len += real_read_len;

			if (real_read_len == 0) {
				break;
			}
		}

		if (total_read_len < buf.length) {
			// In common case, this code cannot be executed
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.unknown, null);
			// byte[]new_buf = new byte[total_read_len];
			// System.arraycopy (buf, 0, new_buf, 0, total_read_len);
			// return new_buf;
		} else {
			return buf;
		}
	}

	public int setBytes(long pos, byte[] bytes, int offset, int len)
			throws SQLException {
		if (conn == null || lobHandle == null) {
			throw new NullPointerException();
		}
		if (pos < 1 || offset < 0 || len < 0) {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_value, null);
		}
		if (offset + len > bytes.length) {
			throw new IndexOutOfBoundsException();
		}

		if (isWritable) {
			if (lobHandle.getLobSize() + 1 != pos) {
				throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.lob_pos_invalid, null);
			}

			pos--; // pos is now offset from 0
			int real_write_len, write_len, total_write_len = 0;

			while (len > 0) {
				write_len = Math.min(len, CLOB_MAX_IO_LENGTH);
				real_write_len = conn.lobWrite(lobHandle.getPackedLobHandle(),
						pos, bytes, offset, write_len);

				pos += real_write_len;
				len -= real_write_len;
				offset += real_write_len;
				total_write_len += real_write_len;
			}

			if (pos > lobHandle.getLobSize()) {
				lobHandle.setLobSize(pos);
				clobCharLength = -1;
			}

			return total_write_len;
		} else {
			throw conn.createCUBRIDException(CUBRIDJDBCErrorCode.lob_is_not_writable, null);
		}
	}

}
