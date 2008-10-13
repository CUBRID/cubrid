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

class UPutByOIDParameter extends UParameter {

/*=======================================================================
 |	PRIVATE VALUES
 =======================================================================*/

private String attributeNames[];

/*=======================================================================
 |	CONSTRUCTOR
 =======================================================================*/

UPutByOIDParameter(String pNames[], Object pValues[])
	throws UJciException
{
  super((pValues != null) ? pValues.length : 0);

  if (pNames == null || pValues == null ||
      pNames.length != pValues.length)
  {
    throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
  }

  byte[] pTypes = new byte[number];
  attributeNames = new String[number];

  for (int i=0 ; i < number ; i++) {
    attributeNames[i] = pNames[i];

    if (pValues[i] == null) {
      pTypes[i] = UUType.U_TYPE_NULL;
    }
    else {
      pTypes[i] = UUType.getObjectDBtype(pValues[i]);
      if (pTypes[i] == UUType.U_TYPE_NULL)
	throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
    }
  }
  setParameters(pTypes, pValues);
}

/*=======================================================================
 |	PACKAGE ACCESS METHODS
 =======================================================================*/

synchronized void writeParameter(UOutputBuffer outBuffer)
	throws UJciException
{
  for (int i=0 ; i < number ; i++) {
    if (attributeNames[i] != null)
      outBuffer.addStringWithNull(attributeNames[i]);
    else
      outBuffer.addNull();
    outBuffer.addByte(types[i]);
    outBuffer.writeParameter(types[i], values[i]);
  }
}

}  // end of class UParameter
