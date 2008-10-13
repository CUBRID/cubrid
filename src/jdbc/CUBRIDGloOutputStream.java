package cubrid.jdbc.driver;

import java.io.*;
import java.sql.SQLException;
import cubrid.sql.CUBRIDOID;

class CUBRIDGloOutputStream extends OutputStream {

/*=======================================================================
 |      PRIVATE CONSTANT VALUES
 =======================================================================*/

private final static int BYTE_BUFFER_SIZE = CUBRIDOID.GLO_MAX_SEND_SIZE;

/*=======================================================================
 |      PRIVATE VARIABLES
 =======================================================================*/

private CUBRIDOID oid;
private int glo_pos;
private int buf_position;
private byte[] data_buffer;
private byte[] write_byte_buf;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

CUBRIDGloOutputStream(CUBRIDOID oid, int pos)
{
  this.oid = oid;
  glo_pos = pos;
  data_buffer = new byte[BYTE_BUFFER_SIZE];
  buf_position = 0;
  write_byte_buf = new byte[1];
}

/*=======================================================================
 |      java.io.OutputStream interface
 =======================================================================*/

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

  if (len < BYTE_BUFFER_SIZE - buf_position) {
    System.arraycopy(b, off, data_buffer, buf_position, len);
    buf_position += len;
    return;
  }

  if (buf_position > 0) {
    int write_len = BYTE_BUFFER_SIZE - buf_position;
    System.arraycopy(b, off, data_buffer, buf_position, write_len);
    write_data(data_buffer, 0, BYTE_BUFFER_SIZE);
    off += write_len;
    len -= write_len;
    buf_position = 0;
  }

  while (len >= BYTE_BUFFER_SIZE) {
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

/*=======================================================================
 |      PRIVATE METHODS
 =======================================================================*/

private void write_data(byte[] buffer, int start, int len) throws IOException
{
  try {
    oid.gloWrite(glo_pos, buffer, start, len);
    glo_pos += len;
  } catch (SQLException e) {
    throw new IOException(e.getMessage());
  }
}

}
