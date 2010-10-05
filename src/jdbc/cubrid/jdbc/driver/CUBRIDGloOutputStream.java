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

package @CUBRID_DRIVER@;

import java.io.*;
import java.sql.SQLException;
import @CUBRID_SQL@.CUBRIDOID;

class CUBRIDGloOutputStream extends OutputStream
{
  private final static int BYTE_BUFFER_SIZE = CUBRIDOID.GLO_MAX_SEND_SIZE;

  private CUBRIDOID oid;
  private int glo_pos;
  private int buf_position;
  private byte[] data_buffer;
  private byte[] write_byte_buf;

  CUBRIDGloOutputStream(CUBRIDOID oid, int pos)
  {
    this.oid = oid;
    glo_pos = pos;
    data_buffer = new byte[BYTE_BUFFER_SIZE];
    buf_position = 0;
    write_byte_buf = new byte[1];
  }

  /*
   * java.io.OutputStream interface
   */

  public synchronized void write(int b) throws IOException
  {
    write_byte_buf[0] = (byte) b;
    write(write_byte_buf, 0, 1);
  }

  public synchronized void write(byte[] b) throws IOException
  {
    write(b, 0, b.length);
  }

  public synchronized void write(byte[] b, int off, int len) throws IOException
  {
    if (oid == null || b == null)
      throw new NullPointerException();
    if (off < 0 || len < 0 || off + len > b.length)
      throw new IndexOutOfBoundsException();

    if (len < BYTE_BUFFER_SIZE - buf_position)
    {
      System.arraycopy(b, off, data_buffer, buf_position, len);
      buf_position += len;
      return;
    }

    if (buf_position > 0)
    {
      int write_len = BYTE_BUFFER_SIZE - buf_position;
      System.arraycopy(b, off, data_buffer, buf_position, write_len);
      write_data(data_buffer, 0, BYTE_BUFFER_SIZE);
      off += write_len;
      len -= write_len;
      buf_position = 0;
    }

    while (len >= BYTE_BUFFER_SIZE)
    {
      write_data(b, off, BYTE_BUFFER_SIZE);
      off += BYTE_BUFFER_SIZE;
      len -= BYTE_BUFFER_SIZE;
    }

    System.arraycopy(b, off, data_buffer, buf_position, len);
    buf_position += len;
  }

  public synchronized void flush() throws IOException
  {
    if (oid == null)
      return;

    if (buf_position <= 0)
      return;

    write_data(data_buffer, 0, buf_position);
    buf_position = 0;
  }

  public synchronized void close() throws IOException
  {
    flush();
    oid = null;
  }

  private void write_data(byte[] buffer, int start, int len) throws IOException
  {
    try
    {
      oid.gloWrite(glo_pos, buffer, start, len);
      glo_pos += len;
    }
    catch (SQLException e)
    {
      throw new IOException(e.getMessage());
    }
  }
}
