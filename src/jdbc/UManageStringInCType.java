
/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.io.IOException;
import java.io.DataInputStream;

/**
* Java는 c와는 달리 string의 끝에 null character를 포함하지 않는다. 이러한
* language간의 string management의 차이를 고려하여 CAS와 string communication을
* manage하는 class이다. 또한 특정 type value를 precision length만큼 space를
* 추가하기 위한 method 도한 포함하고 있다.
*
* since 1.0
*/

abstract class UManageStringInCType {

/* Internal Variable */

  final static String spaceString = new String(" ");

/* Internal Interface */

/* 특정 type은 string representation에서 precision만큼의 길이를 가져야 하는
*   경우가 있다. 이러한 경우를 위해 space를 precision만큼 추가해주는 method이다. */

  static String stringWithSpace(String originalData, int length) {
    if (originalData == null)
      originalData="";
    for (int i = originalData.length() ; i < length ; i++)
      originalData += spaceString;
    return originalData;
  }
}

