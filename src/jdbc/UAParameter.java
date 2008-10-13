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

/**
 * Collection type에 data를 add하거나 delete할 때 JCI에서는 add하고자하는 attribute의
 * collection base type정보를 알 수 없기 때문에 End user로부터 입력받은 data의
 * type으로부터 CUBRID type을
 */

class UAParameter extends UParameter {

/*=======================================================================
 |	PRIVATE VARIABLES
 =======================================================================*/

private String attributeName;

/*=======================================================================
 |	CONSTRUCTOR
 =======================================================================*/

UAParameter(String pName, Object pValue) throws UJciException
{
  super(1);

  byte[] pTypes = new byte[1];
  Object attributeValue[] = new Object[1];

  attributeName = pName;
  attributeValue[0] = pValue;
  if (pValue == null) {
    pTypes[0] = UUType.U_TYPE_NULL;
  }
  else {
    pTypes[0] = UUType.getObjectDBtype(pValue);
    if (pTypes[0] == UUType.U_TYPE_NULL || pTypes[0] == UUType.U_TYPE_SEQUENCE)
      throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
  }

  setParameters(pTypes, attributeValue);
}

/*=======================================================================
 |	PACKAGE ACCESS METHODS
 =======================================================================*/

synchronized void writeParameter(UOutputBuffer outBuffer)
	throws UJciException
{
  if (attributeName != null)
    outBuffer.addStringWithNull(attributeName);
  else
    outBuffer.addNull();
  outBuffer.addByte(types[0]);
  outBuffer.writeParameter(types[0], values[0]);
}

}  // end of class UAParameter
