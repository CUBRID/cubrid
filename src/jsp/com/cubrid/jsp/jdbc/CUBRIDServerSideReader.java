/*
 * Copyright (C) 2008 Search Solution Corporation.
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

import java.io.Reader;

/**
 * Title: CUBRID JDBC Driver Description:
 *
 * @version 2.0
 */

class CUBRIDServerSideReader extends Reader {
    private int position;
    private String valueBuffer;

    CUBRIDServerSideReader(String v) {
        valueBuffer = v;
        position = 0;
    }

    public synchronized int available() throws java.io.IOException {
        if (valueBuffer == null) return 0;
        return valueBuffer.length() - position;
    }

    public synchronized int read(char[] cbuf, int off, int len) throws java.io.IOException {
        if (cbuf == null) throw new NullPointerException();
        else if (off < 0
                || off > cbuf.length
                || len < 0
                || off + len > cbuf.length
                || off + len < 0) throw new IndexOutOfBoundsException();
        else if (len == 0) return 0;

        if (valueBuffer == null) return -1;

        int i;
        for (i = position; i < len + position && i < valueBuffer.length(); i++) {
            cbuf[i - position + off] = valueBuffer.charAt(i);
        }

        int temp = position;
        position = i;
        if (position == valueBuffer.length()) close();

        return i - temp;
    }

    public synchronized void close() throws java.io.IOException {
        valueBuffer = null;
    }

    public boolean ready() {
        return true;
    }
}
