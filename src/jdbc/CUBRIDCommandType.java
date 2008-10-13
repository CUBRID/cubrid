
/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

/**
* CUBRID의 Command Type을 정의하는 class이다.
*
* since 1.0
*/

abstract public class CUBRIDCommandType {
  public final static byte SQLX_CMD_ALTER_CLASS=0;
  public final static byte SQLX_CMD_ALTER_SERIAL=1;
  public final static byte SQLX_CMD_COMMIT_WORK=2;
  public final static byte SQLX_CMD_REGISTER_DATABASE=3;
  public final static byte SQLX_CMD_CREATE_CLASS=4;
  public final static byte SQLX_CMD_CREATE_INDEX=5;
  public final static byte SQLX_CMD_CREATE_TRIGGER=6;
  public final static byte SQLX_CMD_CREATE_SERIAL=7;
  public final static byte SQLX_CMD_DROP_DATABASE=8;
  public final static byte SQLX_CMD_DROP_CLASS=9;
  public final static byte SQLX_CMD_DROP_INDEX=10;
  public final static byte SQLX_CMD_DROP_LABEL=11;
  public final static byte SQLX_CMD_DROP_TRIGGER=12;
  public final static byte SQLX_CMD_DROP_SERIAL=13;
  public final static byte SQLX_CMD_EVALUATE=14;
  public final static byte SQLX_CMD_RENAME_CLASS=15;
  public final static byte SQLX_CMD_ROLLBACK_WORK=16;
  public final static byte SQLX_CMD_GRANT=17;
  public final static byte SQLX_CMD_REVOKE=18;
  public final static byte SQLX_CMD_STATISTICS=19;
  public final static byte SQLX_CMD_INSERT=20;
  public final static byte SQLX_CMD_SELECT=21;
  public final static byte SQLX_CMD_UPDATE=22;
  public final static byte SQLX_CMD_DELETE=23;
  public final static byte SQLX_CMD_CALL=24;
  public final static byte SQLX_CMD_GET_ISO_LVL=25;
  public final static byte SQLX_CMD_GET_TIMEOUT=26;
  public final static byte SQLX_CMD_GET_OPT_LVL=27;
  public final static byte SQLX_CMD_SET_OPT_LVL=28;
  public final static byte SQLX_CMD_SCOPE=29;
  public final static byte SQLX_CMD_GET_TRIGGER=30;
  public final static byte SQLX_CMD_SET_TRIGGER=31;
  public final static byte SQLX_CMD_SAVEPOINT=32;
  public final static byte SQLX_CMD_PREPARE=33;
  public final static byte SQLX_CMD_ATTACH=34;
  public final static byte SQLX_CMD_USE=35;
  public final static byte SQLX_CMD_REMOVE_TRIGGER=36;
  public final static byte SQLX_CMD_RENAME_TRIGGER=37;
  public final static byte SQLX_CMD_ON_LDB=38;
  public final static byte SQLX_CMD_GET_LDB=39;
  public final static byte SQLX_CMD_SET_LDB=40;
  public final static byte SQLX_CMD_GET_STATS=41;

  public final static byte SQLX_CMD_CALL_SP=0x7e;
  public final static byte SQLX_CMD_UNKNOWN=0x7f;
}

