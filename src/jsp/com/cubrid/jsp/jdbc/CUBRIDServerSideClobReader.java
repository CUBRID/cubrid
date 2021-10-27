/*
 * 
 * Copyright (c) 2016 CUBRID Corporation.
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

package com.cubrid.jsp.jdbc;

import java.io.IOException;
import java.io.Reader;
import java.sql.Clob;
import java.sql.SQLException;

class CUBRIDServerSideClobReader extends Reader {
    private Clob clob;
    private long charPos;
    private long charLength;

    CUBRIDServerSideClobReader(Clob clob, long pos, long length) {
        this.clob = clob;
        charPos = pos;
        charLength = pos - 1 + length;
        if (charLength < 0) // overflowed
        {
            charLength = Long.MAX_VALUE;
        }
    }

    public synchronized int read() throws IOException {
        char[] c = new char[1];
        if (read(c, 0, 1) == 1)
            return (0xffff & c[0]);
        else
            return -1;
    }

    public synchronized int read(char[] cbuf) throws IOException {
        return read(cbuf, 0, cbuf.length);
    }

    public synchronized int read(char[] cbuf, int off, int len) throws IOException {
        if (clob == null)
            return -1;

        if (cbuf == null)
            throw new NullPointerException();
        if (off < 0 || len < 0 || off + len > cbuf.length)
            throw new IndexOutOfBoundsException();

        int read_chars;

        try {
            if (charPos - 1 + len > charLength) {
                len = (int) (charLength - charPos + 1);
                if (len < 0)
                    len = 0;
            }

            String str = clob.getSubString(charPos, len);
            str.getChars(0, str.length(), cbuf, off);
            read_chars = str.length();
        } catch (SQLException e) {
            throw new IOException(e.getMessage());
        }

        charPos += read_chars;
        if (read_chars < len || charPos > charLength) {
            clob = null;
        }

        return read_chars;
    }

    public synchronized void close() throws IOException {
        clob = null;
    }
}
