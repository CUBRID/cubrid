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

/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package @CUBRID_JCI@;

import @CUBRID_SQL@.CUBRIDOID;

/**
 * query statement, getSchemaInfo, getByOid를 수행한 뒤 얻어지는 Result Set을
 * manage하기 위한 class이다. 각 column들은 Object array로 관리되며 column
 * value가 null인지를 check하는 boolean type array를 가지고 있다.
 * 
 * since 1.0
 */

class UResultTuple
{
  private int index;
  private CUBRIDOID oid;
  // private boolean wasNull[] = {};
  private Object attributes[];

  UResultTuple(int tupleIndex, int attributeNumber)
  {
    index = tupleIndex;
    attributes = new Object[attributeNumber];
    // wasNull = new boolean[attributeNumber];
  }

  /*
   * Result Set을 clear시키기 위해 사용된다.
   */

  void close()
  {
    for (int i = 0; attributes != null && i < attributes.length; i++)
      attributes[i] = null;
    attributes = null;
    // wasNull = null;
    oid = null;
  }

  /*
   * index번째 column value를 return한다.
   */

  Object getAttribute(int tIndex)
  {
    /*
     * if (tIndex < 0 || attributes == null || tIndex >= attributes.length)
     * return null;
     */
    return attributes[tIndex];
  }

  /*
   * 현재 tuple의 oid값을 return한다.
   */

  CUBRIDOID getOid()
  {
    return oid;
  }

  boolean oidIsIncluded()
  {
    if (oid == null)
      return false;
    return true;
  }

  /*
   * 현재 tuple의 index번째 column value를 data로 set한다.
   */

  void setAttribute(int tIndex, Object data)
  {
    /*
     * if (wasNull == null || attributes == null || tIndex < 0 || tIndex >
     * wasNull.length - 1 || tIndex > attributes.length - 1) { return; }
     * wasNull[tIndex] = (data == null) ? true : false;
     */

    attributes[tIndex] = data;
  }

  /*
   * 현재 tuple의 oid를 set한다.
   */

  void setOid(CUBRIDOID o)
  {
    oid = o;
  }

  /*
   * tuple index를 return한다.
   */

  int tupleNumber()
  {
    return index;
  }

  /*
   * 현재 tuple의 index번째 column이 null인지를 return한다.
   * 
   * 
   * boolean wasNull(int tIndex) { return ((wasNull != null) ? wasNull[tIndex] :
   * false); }
   */
}
