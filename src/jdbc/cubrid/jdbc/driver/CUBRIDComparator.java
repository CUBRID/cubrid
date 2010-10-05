/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

package @CUBRID_DRIVER@;

import java.util.Comparator;

/**
 * Title:        CUBRID JDBC Driver
 * Description:
 * @version 2.0
 */

/**
 * 이 class는 CUBRIDDatabaseMetaData에서 만들어지는 ResultSet인
 * CUBRIDResultSetWithoutQuery의 row들의 sorting을 위한 class로서 sorting에
 * 사용될 비교함수를 구현한 함수이다.
 * 
 * sorting방법은 DatabaseMetaData의 함수에 따라서 다르므로 어떤 함수에서 생성된
 * ResultSet인지 알기 위해서 생성시에 함수이름을 부여받는다. 부여받은 함수에
 * 따라서 다른 비교함수를 호출해준다.
 */

class CUBRIDComparator implements Comparator
{
  /**
   * DatabaseMetaData의 어떤 method에 의해 생성된 ResultSet의 비교함수가 될
   * 것인가를 나타낸다.
   */
  private String dbmd_method;

  /**
   * 비교하게될 ResultSet이 DatabaseMetaData의 어떤 method 에서 생성된 것인가를
   * 알 수 있도록 method의 이름이 constructor에 주어진다.
   */

  CUBRIDComparator(String whatfor)
  {
    dbmd_method = whatfor;
  }

  /*
   * java.util.Comparator interface
   */

  /**
   * 주어진 method이름에 따라서 적절한 비교함수를 호출해준다.
   */
  public int compare(Object o1, Object o2)
  {
    if (dbmd_method.endsWith("getTables"))
      return compare_getTables(o1, o2);
    if (dbmd_method.endsWith("getColumns"))
      return compare_getColumns(o1, o2);
    if (dbmd_method.endsWith("getColumnPrivileges"))
      return compare_getColumnPrivileges(o1, o2);
    if (dbmd_method.endsWith("getTablePrivileges"))
      return compare_getTablePrivileges(o1, o2);
    if (dbmd_method.endsWith("getBestRowIdentifier"))
      return compare_getBestRowIdentifier(o1, o2);
    if (dbmd_method.endsWith("getIndexInfo"))
      return compare_getIndexInfo(o1, o2);
    if (dbmd_method.endsWith("getSuperTables"))
      return compare_getSuperTables(o1, o2);
    return 0;
  }

  /**
   * CUBRIDDatabaseMetaData.getTables()의 비교함수
   */
  private int compare_getTables(Object o1, Object o2)
  {
    int t;
    t = ((String) ((Object[]) o1)[3]).compareTo((String) ((Object[]) o2)[3]);
    if (t != 0)
      return t;
    return ((String) ((Object[]) o1)[2]).compareTo((String) ((Object[]) o2)[2]);
  }

  /**
   * CUBRIDDatabaseMetaData.getColumns()의 비교함수
   */
  private int compare_getColumns(Object o1, Object o2)
  {
    int t;
    t = ((String) ((Object[]) o1)[2]).compareTo((String) ((Object[]) o2)[2]);
    if (t != 0)
      return t;
    return ((Integer) ((Object[]) o1)[16])
        .compareTo((Integer) ((Object[]) o2)[16]);
  }

  /**
   * CUBRIDDatabaseMetaData.getColumnPrivileges()의 비교함수
   */
  private int compare_getColumnPrivileges(Object o1, Object o2)
  {
    int t;
    t = ((String) ((Object[]) o1)[3]).compareTo((String) ((Object[]) o2)[3]);
    if (t != 0)
      return t;
    return ((String) ((Object[]) o1)[6]).compareTo((String) ((Object[]) o2)[6]);
  }

  /**
   * CUBRIDDatabaseMetaData.getTablePrivileges()의 비교함수
   */
  private int compare_getTablePrivileges(Object o1, Object o2)
  {
    int t;
    t = ((String) ((Object[]) o1)[2]).compareTo((String) ((Object[]) o2)[2]);
    if (t != 0)
      return t;
    return ((String) ((Object[]) o1)[5]).compareTo((String) ((Object[]) o2)[5]);
  }

  /**
   * CUBRIDDatabaseMetaData.getBestRowIdentifier()의 비교함수
   */
  private int compare_getBestRowIdentifier(Object o1, Object o2)
  {
    return ((Short) ((Object[]) o1)[0]).compareTo((Short) ((Object[]) o2)[0]);
  }

  /**
   * CUBRIDDatabaseMetaData.getIndexInfo()의 비교함수
   */
  private int compare_getIndexInfo(Object o1, Object o2)
  {
    int t;

    if (((Boolean) ((Object[]) o1)[3]).booleanValue()
        && !((Boolean) ((Object[]) o2)[3]).booleanValue())
      return 1;
    if (!((Boolean) ((Object[]) o1)[3]).booleanValue()
        && ((Boolean) ((Object[]) o2)[3]).booleanValue())
      return -1;

    t = ((Short) ((Object[]) o1)[6]).compareTo((Short) ((Object[]) o2)[6]);
    if (t != 0)
      return t;

    if (((Object[]) o1)[5] == null)
      return 0;
    t = ((String) ((Object[]) o1)[5]).compareTo((String) ((Object[]) o2)[5]);
    if (t != 0)
      return t;

    return ((Integer) ((Object[]) o1)[7])
        .compareTo((Integer) ((Object[]) o2)[7]);
  }

  /**
   * CUBRIDDatabaseMetaData.getSuperTables()의 비교함수
   */
  private int compare_getSuperTables(Object o1, Object o2)
  {
    int t;
    t = ((String) ((Object[]) o1)[2]).compareTo((String) ((Object[]) o2)[2]);
    if (t != 0)
      return t;
    return ((String) ((Object[]) o1)[3]).compareTo((String) ((Object[]) o2)[3]);
  }

}
