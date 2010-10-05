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

class CUBRIDGloInputStream extends InputStream
{
  private final static int BYTE_BUFFER_SIZE = CUBRIDOID.GLO_MAX_SEND_SIZE;

  private CUBRIDOID oid;
  private long glo_pos;
  private int unread_size;
  private int buf_position;
  private byte[] data_buffer;
  private byte[] read_byte_buf;

  CUBRIDGloInputStream(CUBRIDOID oid)
  {
    this.oid = oid;
    data_buffer = new byte[BYTE_BUFFER_SIZE];
    unread_size = 0;
    buf_position = 0;
    glo_pos = 1;
    read_byte_buf = new byte[1];
  }

  /*
   * java.io.InputStream interface
   */

  public synchronized int available() throws IOException
  {
    return (unread_size);
  }

  public synchronized int read() throws IOException
  {
    if (read(read_byte_buf, 0, 1) == 1)
      return (0xff & read_byte_buf[0]);
    else
      return -1;
  }

  public synchronized int read(byte[] b, int off, int len) throws IOException
  {
    if (oid == null && unread_size <= 0)
      return -1;

    if (b == null)
      throw new NullPointerException();
    if (off < 0 || len < 0 || off + len > b.length)
      throw new IndexOutOfBoundsException();

    if (len <= unread_size)
    {
      System.arraycopy(data_buffer, buf_position, b, off, len);
      unread_size -= len;
      buf_position += len;
      return len;
    }

    int copy_size = 0;

    if (unread_size > 0)
    {
      copy_size = unread_size;
      System.arraycopy(data_buffer, buf_position, b, off, copy_size);
      off += copy_size;
      len -= copy_size;
      unread_size = 0;
      buf_position += copy_size;
    }

    while (len >= BYTE_BUFFER_SIZE)
    {
      int read_len = read_data(BYTE_BUFFER_SIZE, b, off);
      if (read_len < BYTE_BUFFER_SIZE)
        return (copy_size + read_len);
      off += read_len;
      len -= read_len;
      copy_size += read_len;
    }

    unread_size = read_data(BYTE_BUFFER_SIZE, data_buffer, 0);
    buf_position = 0;

    if (len > unread_size)
      len = unread_size;

    System.arraycopy(data_buffer, buf_position, b, off, len);
    unread_size -= len;
    buf_position += len;

    return (copy_size + len);
  }

  public synchronized long skip(long n) throws IOException
  {
    if (n <= 0)
      return 0;

    if (n <= unread_size)
    {
      unread_size -= n;
      buf_position += n;
      return n;
    }

    long skip_size, glo_remains_len;

    n -= unread_size;
    skip_size = unread_size;
    unread_size = 0;

    if (oid == null)
      return skip_size;

    try
    {
      glo_remains_len = oid.gloSize() - glo_pos + 1;
    }
    catch (SQLException e)
    {
      throw new IOException(e.getMessage());
    }

    if (n > glo_remains_len)
    {
      n = glo_remains_len;
      oid = null;
    }

    glo_pos += n;
    skip_size += n;

    return skip_size;
  }

  public synchronized void close() throws IOException
  {
    oid = null;
  }

  private int read_data(int len, byte[] buf, int off) throws IOException
  {
    if (oid == null)
      return 0;

    int read_len;

    try
    {
      read_len = oid.gloRead(glo_pos, len, buf, off);
    }
    catch (SQLException e)
    {
      throw new IOException(e.getMessage());
    }

    glo_pos += read_len;
    if (read_len < len)
      oid = null;
    return read_len;
  }

}
