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
import java.io.Writer;
import java.sql.SQLException;

class CUBRIDClobWriter extends Writer {
	private CUBRIDClob clob;
	private long char_pos;

	CUBRIDClobWriter(CUBRIDClob clob, long pos) {
		this.clob = clob;
		char_pos = pos;
	}

	/*
	 * java.io.Writer interface
	 */

	/*
	public synchronized Writer append(CharSequence csq) throws IOException {
		write(csq.toString());
		return this;
	}
	*/

	/*
	public synchronized Writer append(CharSequence csq, int start, int end)
			throws IOException {
		write(csq.subSequence(start, end).toString());
		return this;
	}
	*/

	/*
	public synchronized Writer append(char c) throws IOException {
		write(0xffff & c);
		return this;
	}
	*/

	/*
	public synchronized void write(int c) throws IOException {
		char[] cbuf = new char[1];
		cbuf[0] = (char) c;
		write(cbuf, 0, 1);
	}
	*/

	/*
	public synchronized void write(char[] cbuf) throws IOException {
		write(cbuf, 0, cbuf.length);
	}
	*/

	/*
	public synchronized void write(String str) throws IOException {
		write(str, 0, str.length());
	}
	*/

	public synchronized void write(String str, int off, int len)
			throws IOException {
		if (clob == null)
			throw new IOException();
		if (str == null)
			throw new NullPointerException();
		if (off < 0 || len < 0 || off + len > str.length())
			throw new IndexOutOfBoundsException();

		try {
			char_pos += clob.setString(char_pos, str, off, len);
		} catch (SQLException e) {
			throw new IOException(e.getMessage());
		}
	}

	public synchronized void write(char[] cbuf, int off, int len)
			throws IOException {
		write(new String(cbuf, off, len));
	}

	public synchronized void flush() throws IOException {
	}

	public synchronized void close() throws IOException {
		flush();
		clob.removeFlushableStream(this);
		clob = null;
	}
}
