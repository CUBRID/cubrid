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

package cubrid.jdbc.jci;

import cubrid.sql.CUBRIDOID;

/*
 * resultset을 갖는 statement(execute(SQL query), getSchemaInfo, getByOid를 통해
 * 실행되어지는 statements)들을 실행하였을 때 얻어지는 각 column 정보를 저장하고
 * 있는 class이다.
 * normal statement를 실행했을 때 얻어지는 column 정보는 flag isNullable값과
 * column이 속한 class Name정보를 가지고 있다. getSchemaInfo와 getByOid에 의해
 * 얻어지는 column 정보에 대해 isNullable, getClassName이 call될 때에는 error가
 * set된다.
 *
 * since 1.0
 */

public class UColumnInfo
{
  private final static byte GLO_INSTANCE_FALSE = 0, GLO_INSTANCE_TRUE = 1,
      GLO_INSTANCE_UNKNOWN = -1;

  private byte type;
  private byte collectionBaseType;
  private short scale;
  private int precision;
  private String name, className, attributeName;
  // private String FQDN;
  private boolean isNullable;
  private byte glo_instance_flag;

  UColumnInfo(byte cType, short cScale, int cPrecision, String cName)
  {
    byte realType[];

    realType = UColumnInfo.confirmType(cType);
    type = realType[0];
    collectionBaseType = realType[1];
    scale = cScale;
    precision = cPrecision;
    name = cName;
    className = null;
    attributeName = null;
    isNullable = false;
    // FQDN = UColumnInfo.findFQDN(type, precision, collectionBaseType);
    glo_instance_flag = GLO_INSTANCE_UNKNOWN;
  }

  /*
   * 현재 Column이 nullable인지 아닌지에 대한 flag를 return해준다.
   */

  public boolean isNullable()
  {
    return isNullable;
  }

  /*
   * 현재 column이 속한 class name을 return해 준다.
   */

  public String getClassName()
  {
    return className;
  }

  /*
   * collection type의 경우 JDBC Driver는 각 element의 type이 모두 같은 경우에
   * 얻을 수 있다. 이 때 collection의 base type을 확인하여 알려주는 method이다.
   */

  public int getCollectionBaseType()
  {
    return collectionBaseType;
  }

  /*
   * column name을 return해 준다.
   */

  public String getColumnName()
  {
    return name;
  }

  /*
   * column precision을 return해 준다.
   */

  public int getColumnPrecision()
  {
    return precision;
  }

  /*
   * column scale을 return해 준다.
   */

  public int getColumnScale()
  {
    return (int) scale;
  }

  /*
   * column type을 return해 준다. 이 때 return되어지는 type은 class UUType에
   * 정의된 CUBRID Type이다.
   */

  public byte getColumnType()
  {
    return type;
  }

  /*
   * column value가 Driver를 통해 end-user에게 전해지는 value의 FQDN (Fully
   * Qulified Domain Name)을 return한다.
   */

  public String getFQDN()
  {
    // return FQDN;
    return (UColumnInfo.findFQDN(type, precision, collectionBaseType));
  }

  public String getRealColumnName()
  {
    return attributeName;
  }

  /*
   * collection type의 경우 server쪽에서 넘어오는 type정보가 collection
   * type정보와 collection base type정보가 logical or로 조합되어 넘어온다. 이
   * method는 server쪽으로부터 넘어온 type정보를 읽고 collection type정보와
   * collection base type정보를 따로 추출하여 array로 넘겨준다. (index 0 :
   * collection type, index 1 : collection base type)
   */

  static byte[] confirmType(byte originalType)
  {
    int collectionTypeOrNot = 0;
    byte typeInfo[];

    typeInfo = new byte[2];
    collectionTypeOrNot = originalType & (byte) 0140;
    switch (collectionTypeOrNot)
    {
    case 0:
      typeInfo[0] = originalType;
      typeInfo[1] = -1;
      return typeInfo;
    case 040:
      typeInfo[0] = UUType.U_TYPE_SET;
      typeInfo[1] = (byte) (originalType & 037);
      return typeInfo;
    case 0100:
      typeInfo[0] = UUType.U_TYPE_MULTISET;
      typeInfo[1] = (byte) (originalType & 037);
      return typeInfo;
    case 0140:
      typeInfo[0] = UUType.U_TYPE_SEQUENCE;
      typeInfo[1] = (byte) (originalType & 037);
      return typeInfo;
    default:
      typeInfo[0] = UUType.U_TYPE_NULL;
      typeInfo[1] = -1;
    }
    return typeInfo;
  }

  /*
   * normal statement인 경우 flag isNullable와 column이 속한 class name 정보
   * 또한 set한다.
   */

  synchronized void setRemainedData(String aName, String cName, boolean hNull)
  {
    attributeName = aName;
    className = cName;
    isNullable = hNull;
  }

  boolean isGloInstance(UConnection u_con, CUBRIDOID oid)
  {
    if (glo_instance_flag == GLO_INSTANCE_UNKNOWN)
    {
      Object obj;
      synchronized (u_con)
      {
        obj = u_con.oidCmd(oid, UConnection.IS_GLO_INSTANCE);
      }
      if (obj == null)
        glo_instance_flag = GLO_INSTANCE_FALSE;
      else
        glo_instance_flag = GLO_INSTANCE_TRUE;
    }

    if (glo_instance_flag == GLO_INSTANCE_TRUE)
      return true;

    return false;
  }

  /*
   * 주어진 type과 precision, collection base type정보를 가지고 end-user에게
   * 던져지는 column value의 FQDN(Fully Qulified Domain Name)을 발견한다.
   */

  private static String findFQDN(byte cType, int cPrecision, byte cBaseType)
  {
    switch (cType)
    {
    case UUType.U_TYPE_NULL:
      return "null";
    case UUType.U_TYPE_BIT:
      return (cPrecision == 8) ? "java.lang.Boolean" : "byte[]";
    case UUType.U_TYPE_VARBIT:
      return "byte[]";
    case UUType.U_TYPE_CHAR:
    case UUType.U_TYPE_NCHAR:
    case UUType.U_TYPE_VARCHAR:
    case UUType.U_TYPE_VARNCHAR:
      return "java.lang.String";
    case UUType.U_TYPE_NUMERIC:
      return "java.lang.Double";
    case UUType.U_TYPE_SHORT:
      return "java.lang.Short";
    case UUType.U_TYPE_INT:
      return "java.lang.Integer";
    case UUType.U_TYPE_FLOAT:
      return "java.lang.Float";
    case UUType.U_TYPE_MONETARY:
    case UUType.U_TYPE_DOUBLE:
      return "java.lang.Double";
    case UUType.U_TYPE_DATE:
      return "java.sql.Date";
    case UUType.U_TYPE_TIME:
      return "java.sql.Time";
    case UUType.U_TYPE_TIMESTAMP:
      return "java.sql.Timestamp";
    case UUType.U_TYPE_SET:
    case UUType.U_TYPE_SEQUENCE:
    case UUType.U_TYPE_MULTISET:
      break;
    case UUType.U_TYPE_OBJECT:
      return "cubrid.sql.CUBRIDOID";
    default:
      return "";
    }
    switch (cBaseType)
    {
    case UUType.U_TYPE_NULL:
      return "null";
    case UUType.U_TYPE_BIT:
      return (cPrecision == 8) ? "java.lang.Boolean[]" : "byte[][]";
    case UUType.U_TYPE_VARBIT:
      return "byte[][]";
    case UUType.U_TYPE_CHAR:
    case UUType.U_TYPE_NCHAR:
    case UUType.U_TYPE_VARCHAR:
    case UUType.U_TYPE_VARNCHAR:
      return "java.lang.String[]";
    case UUType.U_TYPE_NUMERIC:
      return "java.lang.Double[]";
    case UUType.U_TYPE_SHORT:
      return "java.lang.Short[]";
    case UUType.U_TYPE_INT:
      return "java.lang.Integer[]";
    case UUType.U_TYPE_FLOAT:
      return "java.lang.Float[]";
    case UUType.U_TYPE_MONETARY:
    case UUType.U_TYPE_DOUBLE:
      return "java.lang.Double[]";
    case UUType.U_TYPE_DATE:
      return "java.sql.Date[]";
    case UUType.U_TYPE_TIME:
      return "java.sql.Time[]";
    case UUType.U_TYPE_TIMESTAMP:
      return "java.sql.Timestamp[]";
    case UUType.U_TYPE_SET:
    case UUType.U_TYPE_SEQUENCE:
    case UUType.U_TYPE_MULTISET:
      break;
    case UUType.U_TYPE_OBJECT:
      return "cubrid.sql.CUBRIDOID[]";
    default:
      break;
    }
    return null;
  }
}
