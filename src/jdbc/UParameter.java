/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;

import cubrid.sql.CUBRIDOID;

abstract class UParameter {

UParameter(int pNumber)
{
  number = pNumber;
  types = new byte[number];
  values = new Object[number];
}

synchronized void setParameters(byte[] pTypes, Object[] pValues)
	throws UJciException
{
  if (pTypes == null || (pValues != null && pTypes.length != pValues.length))
    throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);

  for (int i=0 ; i < number ; i++) {
    types[i] = pTypes[i];
    if (UUType.isCollectionType(types[i]))
      values[i] = new CUBRIDArray(pValues[i]);
    else
      values[i] = pValues[i];
  }
}

abstract void writeParameter(UOutputBuffer outBuffer)
	throws UJciException;

int number;
byte types[];
Object values[];

}  // end of UParameter
