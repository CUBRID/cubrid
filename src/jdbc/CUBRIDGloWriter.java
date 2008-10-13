package cubrid.jdbc.driver;

import java.io.*;
import java.sql.SQLException;

class CUBRIDGloWriter extends Writer {

/*=======================================================================
 |      PRIVATE CONSTANT VALUES
 =======================================================================*/
    
private final static int CHAR_BUFFER_SIZE = CUBRIDClob.READ_CHAR_BUFFER_SIZE;

/*=======================================================================
 |      PRIVATE VARIABLES
 =======================================================================*/

private CUBRIDClob clob;
private int clob_pos;
private int buf_position;
private char[] data_buffer;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

CUBRIDGloWriter(CUBRIDClob clob, int pos)
{
  this.clob = clob;
  clob_pos = pos;
  data_buffer = new char[CHAR_BUFFER_SIZE];
  buf_position = 0;
}

/*=======================================================================
 |      java.io.Writer interface
 =======================================================================*/

public synchronized void write(char[] cbuf, int off, int len) throws IOException
{
  if (clob == null || cbuf == null)
    throw new NullPointerException();

  if (off < 0 || len < 0 || off + len > cbuf.length)
    throw new IndexOutOfBoundsException();

  if (len < CHAR_BUFFER_SIZE - buf_position) {
    System.arraycopy(cbuf, off, data_buffer, buf_position, len);
    buf_position += len;
    return;
  }

  if (buf_position > 0) {
    int write_len = CHAR_BUFFER_SIZE - buf_position;
    System.arraycopy(cbuf, off, data_buffer, buf_position, write_len);
    write_data(data_buffer, 0, CHAR_BUFFER_SIZE);
    off += write_len;
    len -= write_len;
    buf_position = 0;
  }

  if (len >= CHAR_BUFFER_SIZE) {
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

  if (buf_position > 0) {
    write_data(data_buffer, 0, buf_position);
    buf_position = 0;
  }
}

public synchronized void close() throws IOException
{
  flush();
  clob = null;
}

/*=======================================================================
 |      PRIVATE METHODS
 =======================================================================*/

private void write_data(char[] buf, int start, int len) throws IOException
{
  try {
    clob.setString(clob_pos, new String(buf, start, len));
    clob_pos += len;
  } catch (SQLException e) {
    throw new IOException(e.getMessage());
  }
}

} // end of class CUBRIDGloWriter
