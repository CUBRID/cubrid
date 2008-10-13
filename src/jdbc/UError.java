/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

public class UError {

public final static int JCI_ERROR_CODE_BASE = -2000;
public final static int DRIVER_ERROR_CODE_BASE = -2100;
public final static int METHOD_USER_ERROR_BASE = -10000;

/*=======================================================================
 |	PRIVATE VARIABLES
 =======================================================================*/

private int jciErrorCode;
private int serverErrorCode;
private String errorMessage;

/*=======================================================================
 |	CONSTRUCTOR
 =======================================================================*/

UError()
{
  jciErrorCode = UErrorCode.ER_NO_ERROR;
}

public UError(UError src)
{
  copyValue(src);
}

/*=======================================================================
 |	PUBLIC METHODS
 =======================================================================*/

public int getErrorCode()
{
  return jciErrorCode;
}

public String getErrorMsg()
{
  return errorMessage;
}

public int getJdbcErrorCode()
{
  if (jciErrorCode == UErrorCode.ER_NO_ERROR)
    return UErrorCode.ER_NO_ERROR;
  else if (jciErrorCode == UErrorCode.ER_DBMS)
    return serverErrorCode;
  else
    return (JCI_ERROR_CODE_BASE - jciErrorCode);
}

/*=======================================================================
 |	PACKAGE ACCESS METHODS
 =======================================================================*/

void copyValue(UError object)
{
  jciErrorCode = object.jciErrorCode;
  errorMessage = object.errorMessage;
  serverErrorCode = object.serverErrorCode;
}

synchronized void setDBError(int code, String message)
{
  jciErrorCode = UErrorCode.ER_DBMS;
  serverErrorCode = code;
  errorMessage = message;
}

synchronized void setErrorCode(int code)
{
  jciErrorCode = code;
  if (code != UErrorCode.ER_NO_ERROR) {
    errorMessage = UErrorCode.codeToMessage(code);
  }
}

void setErrorMessage(int code, String addMessage)
{
  setErrorCode(code);
  errorMessage += ":" + addMessage;
}

void clear()
{
  jciErrorCode = UErrorCode.ER_NO_ERROR;
}

} // end of class UError
