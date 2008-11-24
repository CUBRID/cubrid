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

import java.io.*;
import java.sql.SQLException;

class CUBRIDGloReader extends Reader
{
  private final static int CHAR_BUFFER_SIZE = CUBRIDClob.READ_CHAR_BUFFER_SIZE;

  private CUBRIDClob u_clob;
  private int buf_position;
  private int clob_pos;
  private int unread_size;
  private char[] data_buffer;

  CUBRIDGloReader(CUBRIDClob clob)
  {
    u_clob = clob;
    data_buffer = new char[CHAR_BUFFER_SIZE];
    unread_size = 0;
    buf_position = 0;
    clob_pos = 1;
  }

  public synchronized int read(char[] cbuf, int off, int len)
      throws java.io.IOException
  {
    if (u_clob == null && unread_size <= 0)
      return -1;

    if (cbuf == null)
      throw new NullPointerException();
    if (off < 0 || len < 0 || off + len > cbuf.length)
      throw new IndexOutOfBoundsException();

    if (len <= unread_size)
    {
      System.arraycopy(data_buffer, buf_position, cbuf, off, len);
      unread_size -= len;
      buf_position += len;
      return len;
    }

    int copy_size = unread_size;

    if (unread_size > 0)
    {
      System.arraycopy(data_buffer, buf_position, cbuf, off, copy_size);
      off += copy_size;
      len -= copy_size;
      unread_size = 0;
      buf_position = 0;
    }

    if (len >= CHAR_BUFFER_SIZE)
    {
      return (copy_size + read_data(len, cbuf, off));
    }

    unread_size = read_data(CHAR_BUFFER_SIZE, data_buffer, 0);
    buf_position = 0;

    if (len > unread_size)
      len = unread_size;

    System.arraycopy(data_buffer, 0, cbuf, off, len);
    buf_position += len;
    unread_size -= len;

    return (copy_size + len);
  }

  public synchronized void close() throws java.io.IOException
  {
    u_clob = null;
  }

  public synchronized boolean ready()
  {
    return (unread_size > 0 ? true : false);
  }

  private int read_data(int len, char[] buffer, int start) throws IOException
  {
    if (u_clob == null)
      return 0;

    int read_len;

    try
    {
      read_len = u_clob.getChars(clob_pos, len, buffer, start);
    }
    catch (SQLException e)
    {
      throw new IOException(e.getMessage());
    }

    clob_pos += read_len;
    if (read_len < len)
      u_clob = null;
    return read_len;
  }
}
