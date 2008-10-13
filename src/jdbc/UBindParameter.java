/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.util.Vector;

/*
 * PreparedStatement에 사용되는 parameter들을 관리하는 class이다.
 * 1.0에서 parameter를 관리하던 class UBindParameterInfo가 1차원으로 parameter를
 * 관리하여 2.0에서는 사용하기가 곤란하여 만들어진 대체 class이다.
 * parameter는 1차원 array를 element로 갖는 java.util.Vector class로 관리된다.
 *
 * since 2.0
 */

class UBindParameter extends UParameter {

/*=======================================================================
 |	PRIVATE CONSTANT VALUES
 =======================================================================*/

private final static byte PARAM_MODE_UNKNOWN = 0;
private final static byte PARAM_MODE_IN = 1;
private final static byte PARAM_MODE_OUT = 2;
private final static byte PARAM_MODE_INOUT = 3;

/*=======================================================================
 |	PACKAGE ACCESS VARIABLES
 =======================================================================*/

byte paramMode[];

/*=======================================================================
 |	PRIVATE VARIABLES
 =======================================================================*/

private boolean isBinded[];

/*=======================================================================
 |	PUBLIC CONSTANT VALUES
 =======================================================================*/

UBindParameter(int parameterNumber)
{
  super(parameterNumber);

  isBinded = new boolean[number];
  paramMode = new byte[number];

  clear();
}

/*=======================================================================
 |	PACKAGE ACCESS METHODS
 =======================================================================*/

/*
 * parameter의 current cursor에서 모든 parameter들이 bind되었는지를 check한다.
 */
boolean checkAllBinded()
{
  for(int i=0 ; i < number ; i++) {
    if (isBinded[i] == false && paramMode[i] == PARAM_MODE_UNKNOWN)
      return false;
  }
  return true;
}

void clear()
{
  for (int i=0 ; i < number ; i++) {
    isBinded[i] = false;
    paramMode[i] = PARAM_MODE_UNKNOWN;
    values[i] = null;
    types[i] = UUType.U_TYPE_NULL;
  }
}

synchronized void close()
{
  for (int i=0 ; i < number ; i++)
    values[i] = null;
  isBinded = null;
  paramMode = null;
  values = null;
  types = null;
}

/*
 * current cursor의 index번 째 parameter value를 set한다.
 */
synchronized void setParameter(int index, byte bType, Object bValue)
	throws UJciException
{
  if (index < 0 || index >= number)
    throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);

  types[index] = bType;
  values[index] = bValue;

  isBinded[index] = true;
  paramMode[index] |= PARAM_MODE_IN;
}

void setOutParam(int index) throws UJciException
{
  if (index < 0 || index >= number)
    throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
  paramMode[index] |= PARAM_MODE_OUT;
}

synchronized void writeParameter(UOutputBuffer outBuffer)
	throws UJciException
{
  for (int i=0 ; i < number ; i++) {
    if (values[i] == null) {
      outBuffer.addByte(UUType.U_TYPE_NULL);
      outBuffer.addNull();
    }
    else {
      outBuffer.addByte((byte) types[i]);
      outBuffer.writeParameter(((byte) types[i]), values[i]);
    }
  }
}

}  // end of class UBindParameter
