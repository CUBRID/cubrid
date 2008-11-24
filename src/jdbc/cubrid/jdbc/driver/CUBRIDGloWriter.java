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

class CUBRIDGloWriter extends Writer
{
  private final static int CHAR_BUFFER_SIZE = CUBRIDClob.READ_CHAR_BUFFER_SIZE;

  private CUBRIDClob clob;
  private int clob_pos;
  private int buf_position;
  private char[] data_buffer;

  CUBRIDGloWriter(CUBRIDClob clob, int pos)
  {
    this.clob = clob;
    clob_pos = pos;
    data_buffer = new char[CHAR_BUFFER_SIZE];
    buf_position = 0;
  }

  /*
   * java.io.Writer interface
   */

  public synchronized void write(char[] cbuf, int off, int len)
      throws IOException
  {
    if (clob == null || cbuf == null)
      throw new NullPointerException();

    if (off < 0 || len < 0 || off + len > cbuf.length)
      throw new IndexOutOfBoundsException();

    if (len < CHAR_BUFFER_SIZE - buf_position)
    {
      System.arraycopy(cbuf, off, data_buffer, buf_position, len);
      buf_position += len;
      return;
    }

    if (buf_position > 0)
    {
      int write_len = CHAR_BUFFER_SIZE - buf_position;
      System.arraycopy(cbuf, off, data_buffer, buf_position, write_len);
      write_data(data_buffer, 0, CHAR_BUFFER_SIZE);
      off += write_len;
      len -= write_len;
      buf_position = 0;
    }

    if (len >= CHAR_BUFFER_SIZE)
    {
      write_data(cbuf, off, len);
      return;
    }

    System.arraycopy(cbuf, off, data_buffer, buf_position, len);
    buf_position += len;
  }

  public synchronized void flush() throws IOException
  {
    if (clob == null)
      return;

    if (buf_position > 0)
    {
      write_data(data_buffer, 0, buf_position);
      buf_position = 0;
    }
  }

  public synchronized void close() throws IOException
  {
    flush();
    clob = null;
  }

  private void write_data(char[] buf, int start, int len) throws IOException
  {
    try
    {
      clob.setString(clob_pos, new String(buf, start, len));
      clob_pos += len;
    }
    catch (SQLException e)
    {
      throw new IOException(e.getMessage());
    }
  }
}
