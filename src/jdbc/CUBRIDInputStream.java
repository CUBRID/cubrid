package cubrid.jdbc.driver;

import java.io.*;


/**
 * Title:        CUBRID JDBC Driver
 * Description:
 * @version 2.0
 */

class CUBRIDInputStream extends InputStream {

/*=======================================================================
 |      PRIVATE VARIABLES 
 =======================================================================*/

private int position;
private byte[] valueBuffer;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

CUBRIDInputStream(byte[] v)
{
  valueBuffer = v;
  position = 0;
}

/*=======================================================================
 |      PUBLIC METHODS
 =======================================================================*/

public synchronized int available() throws java.io.IOException
{
  if (valueBuffer == null) return 0;
  return valueBuffer.length - position;
}

public synchronized int read() throws java.io.IOException
{
  byte b[] = new byte[1];
  if (read(b, 0, 1) == -1)
    return -1;
  else
    return b[0];
}

public synchronized int read(byte[] b, int off, int len) throws java.io.IOException
{
  if (b == null)
    throw new NullPointerException();
  else if (off < 0 || off > b.length || len < 0 ||
	    off + len > b.length || off + len < 0)
    throw new IndexOutOfBoundsException();
  else if (len == 0)
    return 0;

  if (valueBuffer == null) return -1;

  int i;
  for (i=position ; i<len+position && i<valueBuffer.length ; i++) {
    b[i-position+off] = valueBuffer[i];
  }

  int temp = position;
  position = i;
  if (position == valueBuffer.length) close();

  return i-temp;
}

public synchronized void close() throws java.io.IOException
{
  valueBuffer = null;
}

}  // end of class CUBRIDInputStream
