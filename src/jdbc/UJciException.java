package cubrid.jdbc.jci;

class UJciException extends Exception {

private int jciErrCode;
private int serverErrCode;

UJciException(int err)
{
  super();
  jciErrCode = err;
}

UJciException(int err, int srv_err, String msg)
{
  super(msg);
  jciErrCode = err;
  serverErrCode = srv_err;
  if (serverErrCode <= UError.METHOD_USER_ERROR_BASE)
    serverErrCode = UError.METHOD_USER_ERROR_BASE - serverErrCode;
}

void toUError(UError error)
{
  if (jciErrCode == UErrorCode.ER_DBMS) {
    String msg;
    if (serverErrCode > -1000)
      msg = getMessage();
    else
      msg = UErrorCode.codeToCASMessage(serverErrCode);

    error.setDBError(serverErrCode, msg);
  }
  else if (jciErrCode == UErrorCode.ER_UNKNOWN) {
    error.setErrorMessage(jciErrCode, getMessage());
  }
  else {
    error.setErrorCode(jciErrCode);
  }
}

}

