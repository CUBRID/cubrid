package cubrid.jdbc.jci;

abstract public class UJCIUtil {

/*=======================================================================
 |	PUBLIC METHODS
 =======================================================================*/

static public int bytes2int(byte[] b, int startIndex)
{
  int	data = 0;
  int	endIndex = startIndex + 4;

  for (int i=startIndex ; i < endIndex ; i++) {
    data <<= 8;
    data |= (b[i] & 0xff);
  }

  return data;
}

static public short bytes2short(byte[] b, int startIndex)
{
  short	data = 0;
  int endIndex = startIndex + 2;

  for (int i=startIndex ; i < endIndex ; i++) {
    data <<= 8;
    data |= (b[i] & 0xff);
  }
  return data;
}

static public void copy_byte(byte[] dest, int dIndex, int cpSize, String src)
{
  if (src == null)
    return;

  byte[] b = src.getBytes();
  cpSize = (cpSize > b.length) ? b.length : cpSize;
  System.arraycopy(b, 0, dest, dIndex, cpSize);
}

} // end of class UJCIUtil
