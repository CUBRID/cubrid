package cubrid.jdbc.driver;

import java.sql.*;
import java.io.*;

import cubrid.sql.CUBRIDOID;

public class CUBRIDBlob extends CUBRIDOID implements Blob {

/*=======================================================================
 |      CONSTANT VALUES
 =======================================================================*/

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

public CUBRIDBlob(CUBRIDOID o)
{
  super(o);
}

/*=======================================================================
 |      java.sql.Blob interface
 =======================================================================*/

public long length() throws SQLException
{
  return (gloSize());
}

public byte[] getBytes(long pos, int length) throws SQLException
{
  byte[] b = new byte[length];

  int read_len = gloRead(pos, length, b, 0);

  if (read_len < length) {
    byte[] cpbuf = new byte[read_len];
    System.arraycopy(b, 0, cpbuf, 0, read_len);
    b = cpbuf;
  }

  return b;
}

public InputStream getBinaryStream() throws SQLException
{
  return (new CUBRIDGloInputStream(this));
}

public long position(byte[] pattern, long start) throws SQLException
{
  return (gloBinarySearch(start, pattern, 0, pattern.length));
}

public long position(Blob pattern, long start) throws SQLException
{
  return (position(pattern.getBytes(1, GLO_MAX_SEARCH_LEN), start));
}

public int setBytes(long pos, byte[] bytes) throws SQLException
{
  return (setBytes(pos, bytes, 0, bytes.length));
}

public int setBytes(long pos, byte[] bytes, int offset, int len)
	throws SQLException
{
  return (gloWrite(pos, bytes, offset, len));
}

public OutputStream setBinaryStream(long pos) throws SQLException
{
  return (new CUBRIDGloOutputStream(this, (int) pos));
}

public void truncate(long len) throws SQLException
{
  gloTruncate(len);
}

} // end of class CUBRIDBlob
