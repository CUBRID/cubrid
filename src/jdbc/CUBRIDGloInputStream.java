package cubrid.jdbc.driver;

import java.io.*;
import java.sql.SQLException;
import cubrid.sql.CUBRIDOID;

class CUBRIDGloInputStream extends InputStream {

/*=======================================================================
 |      PRIVATE CONSTANT VALUES
 =======================================================================*/

private final static int BYTE_BUFFER_SIZE = CUBRIDOID.GLO_MAX_SEND_SIZE;

/*=======================================================================
 |      PRIVATE VARIABLES 
 =======================================================================*/

private CUBRIDOID oid;
private long glo_pos;
private int unread_size;
private int buf_position;
private byte[] data_buffer;
private byte[] read_byte_buf;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

CUBRIDGloInputStream(CUBRIDOID oid)
{
  this.oid = oid;
  data_buffer = new byte[BYTE_BUFFER_SIZE];
  unread_size = 0;
  buf_position = 0;
  glo_pos = 1;
  read_byte_buf = new byte[1];
}

/*=======================================================================
 |      java.io.InputStream interface
 =======================================================================*/

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

  if (len <= unread_size) {
    System.arraycopy(data_buffer, buf_position, b, off, len);
    unread_size -= len;
    buf_position += len;
    return len;
  }

  int copy_size = 0;

  if (unread_size > 0) {
    copy_size = unread_size;
    System.arraycopy(data_buffer, buf_position, b, off, copy_size);
    off += copy_size;
    len -= copy_size;
    unread_size = 0;
    buf_position += copy_size;
  }

  while (len >= BYTE_BUFFER_SIZE) {
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

  if (n <= unread_size) {
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

  try {
    glo_remains_len = oid.gloSize() - glo_pos + 1;
  } catch (SQLException e) {
    throw new IOException(e.getMessage());
  }

  if (n > glo_remains_len) {
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

/*=======================================================================
 |      PRIVATE METHODS
 =======================================================================*/

private int read_data(int len, byte[] buf, int off) throws IOException
{
  if (oid == null)
    return 0;

  int read_len;

  try {
    read_len = oid.gloRead(glo_pos, len, buf, off);
  } catch (SQLException e) {
    throw new IOException(e.getMessage());
  }

  glo_pos += read_len;
  if (read_len < len)
    oid = null;
  return read_len;
}

}  // end of class CUBRIDGloInputStream
