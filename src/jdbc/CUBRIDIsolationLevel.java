
/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

/**
* CUBRID의 Isolation level을 정의한 class이다.
*
* since 1.0
*/

abstract public class CUBRIDIsolationLevel {
  public final static int TRAN_MIN = 1;
  public final static int TRAN_MAX = 6;
  public final static int TRAN_UNKNOWN_ISOLATION = 0;
  public final static int TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE = 1;
  public final static int TRAN_COMMIT_CLASS_COMMIT_INSTANCE = 2;
  /* TRAN_READ_UNCOMMITTED */
  public final static int TRAN_REP_CLASS_UNCOMMIT_INSTANCE = 3;
  /* TRAN_READ_COMMITTED */
  public final static int TRAN_REP_CLASS_COMMIT_INSTANCE = 4;
  /* TRAN_SERIALIZABLE */
  public final static int TRAN_REP_CLASS_REP_INSTANCE = 5;
  public final static int TRAN_SERIALIZABLE = 6;
}

