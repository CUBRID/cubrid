package cubrid.jdbc.driver;

import java.sql.SQLException;

import cubrid.jdbc.driver.CUBRIDJDBCErrorCode;
import cubrid.jdbc.jci.UError;

public class CUBRIDException extends SQLException {

protected CUBRIDException(String msg, int errCode)
{
  super(msg, null, errCode);

/*
  System.err.println("------------ CUBRID JDBC DRIVER -------------");
  printStackTrace();
  System.err.println("---------------------------------------------");
*/
}

public CUBRIDException(UError error)
{
  this(error.getErrorMsg(), error.getJdbcErrorCode());
}

public CUBRIDException(int errCode)
{
  this(CUBRIDJDBCErrorCode.getMessage(errCode),
       UError.DRIVER_ERROR_CODE_BASE - errCode);
}

public CUBRIDException(int errCode, String msg)
{
  this(CUBRIDJDBCErrorCode.getMessage(errCode) + msg,
         UError.DRIVER_ERROR_CODE_BASE - errCode);
}

}

