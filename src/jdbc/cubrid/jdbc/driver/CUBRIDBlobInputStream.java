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

import java.io.IOException;
import java.io.InputStream;
import java.sql.SQLException;

class CUBRIDBlobInputStream extends InputStream {
	private CUBRIDBlob blob;
	private long lob_pos;
	private long lob_length;

	CUBRIDBlobInputStream(CUBRIDBlob blob, long pos, long length)
			throws SQLException {
		this.blob = blob;
		lob_pos = pos;
		lob_length = pos - 1 + length;
		if (lob_length > blob.length() || lob_length < 0) // overflowed
		{
			lob_length = blob.length();
		}
	}

	/*
	 * java.io.InputStream interface
	 */

	/*
	public int available() throws IOException {
		return 0;
	}
	*/

	public synchronized int read() throws IOException {
		byte[] b = new byte[1];
		if (read(b, 0, 1) == 1)
			return (0xff & b[0]);
		else
			return -1;
	}

	/*
	public synchronized int read(byte[] b) throws IOException {
		return read(b, 0, b.length);
	}
	*/

	public synchronized int read(byte[] b, int off, int len) throws IOException {
		if (blob == null)
			return -1;

		if (b == null)
			throw new NullPointerException();
		if (off < 0 || len < 0 || off + len > b.length)
			throw new IndexOutOfBoundsException();

		int read_len;

		try {
			if (lob_pos - 1 + len > lob_length) {
				len = (int) (lob_length - lob_pos + 1);
				if (len < 0)
					len = 0;
			}

			byte[] buf = blob.getBytes(lob_pos, len);
			System.arraycopy(buf, 0, b, off, buf.length);
			read_len = buf.length;
		} catch (SQLException e) {
			throw new IOException(e.getMessage());
		}

		lob_pos += read_len;
		if (read_len < len || lob_pos > lob_length) {
			blob = null;
		}

		return read_len;
	}

	public synchronized long skip(long n) throws IOException {
		if (n <= 0)
			return 0;

		if (blob == null)
			return 0;

		long lob_remains_len = lob_length - lob_pos + 1;
		if (n > lob_remains_len) {
			n = lob_remains_len;
			blob = null;
		}

		lob_pos += n;

		return n;
	}

	public synchronized void close() throws IOException {
		blob = null;
	}

	/*
	public void mark(int readlimit) {
	}
	*/

	/*
	public void reset() throws IOException {
		throw new IOException("Not supported mark and reset operation");
	}
	*/

	/*
	public boolean markSupported() {
		return false;
	}
	*/
}
