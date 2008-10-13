package cubrid.jdbc.driver;

import java.io.*;
import java.sql.SQLException;

class CUBRIDGloReader extends Reader {

/*=======================================================================
 |      PRIVATE CONSTANT VALUES
 =======================================================================*/

private final static int CHAR_BUFFER_SIZE = CUBRIDClob.READ_CHAR_BUFFER_SIZE;

/*=======================================================================
 |      PRIVATE VARIABLES 
 =======================================================================*/

private CUBRIDClob u_clob;
private int buf_position;
private int clob_pos;
private int unread_size;
private char[] data_buffer;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

CUBRIDGloReader(CUBRIDClob clob)
{
  u_clob = clob;
  data_buffer = new char[CHAR_BUFFER_SIZE];
  unread_size = 0;
  buf_position = 0;
  clob_pos = 1;
}

/*=======================================================================
 |      PUBLIC METHODS
 =======================================================================*/

public synchronized int read(char[] cbuf, int off, int len) throws java.io.IOException
{
  if (u_clob == null && unread_size <= 0)
    return -1;

  if (cbuf == null)
    throw new NullPointerException(); 
  if (off < 0 || len < 0 || off + len > cbuf.length)
    throw new IndexOutOfBoundsException();

  if (len <= unread_size) {
    System.arraycopy(data_buffer, buf_position, cbuf, off, len);
    unread_size -= len;
    buf_position += len;
    return len;
  }

  int copy_size = unread_size;

  if (unread_size > 0) {
    System.arraycopy(data_buffer, buf_position, cbuf, off, copy_size);
    off += copy_size;
    len -= copy_size;
    unread_size = 0;
    buf_position = 0;
  }

  if (len >= CHAR_BUFFER_SIZE) {
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

/*=======================================================================
 |      PRIVATE METHODS
 =======================================================================*/

private int read_data(int len, char[] buffer, int start) throws IOException
{
  if (u_clob == null)
    return 0;

  int read_len;

  try {
    read_len = u_clob.getChars(clob_pos, len, buffer, start);
  } catch (SQLException e) {
    throw new IOException(e.getMessage());
  }

  clob_pos += read_len;
  if (read_len < len)
    u_clob = null;
  return read_len;
}

}  // end of class CUBRIDGloReader
