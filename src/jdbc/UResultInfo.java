/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import cubrid.sql.CUBRIDOID;

/**
* Statement를 execute후 얻어지는 Result들에 관련된 정보를 관리하는 class이다.
* 각 statement result마다 하나의 UResultInfo instance가 result info를 관리한다.
*
* since 2.0
*/

public class UResultInfo {

/*=======================================================================
 |	PRIVATE VARIABLES
 =======================================================================*/

byte statementType;
private int resultCount;
private boolean isResultSet; /* if result is resultset, true. otherwise false */
private CUBRIDOID oid;
private long srv_cache_time;

/*=======================================================================
 |	CONSTRUCTOR
 =======================================================================*/

UResultInfo(byte type, int count)
{
  statementType = type;
  resultCount = count;
  if (statementType == CUBRIDCommandType.SQLX_CMD_SELECT ||
      statementType == CUBRIDCommandType.SQLX_CMD_CALL ||
      statementType == CUBRIDCommandType.SQLX_CMD_GET_STATS ||
      statementType == CUBRIDCommandType.SQLX_CMD_EVALUATE)
  {
    isResultSet = true;
  }
  else {
    isResultSet = false;
  }
  oid = null;
  srv_cache_time = 0L;
}

/*=======================================================================
 |	PUBLIC METHODS
 =======================================================================*/

/*
 *Statement를 execute하여 얻어지는 result count를 return한다.
 */

public int getResultCount()
{
  return resultCount;
}

/*
 * 실행된 Statement이 ResultSet을 result로 갖는 query문이거나 method call,
 * evaluate문인지 아닌지를 판단한다. ResultSet인 경우 true, 그렇지 않은 경우
 * false를 return한다.
 */

public boolean isResultSet()
{
  return isResultSet;
}

/*=======================================================================
 |	PACKAGE ACCESS METHODS
 =======================================================================*/

CUBRIDOID getCUBRIDOID()
{
  return oid;
}

void setResultOid(CUBRIDOID o)
{
  if (statementType == CUBRIDCommandType.SQLX_CMD_INSERT && resultCount == 1)
    oid = o;
}

void setSrvCacheTime(int sec, int usec)
{
  srv_cache_time = sec;
  srv_cache_time = (srv_cache_time << 32) | usec;
}

long getSrvCacheTime()
{
  return srv_cache_time;
}

}  // end of class UResultInfo
