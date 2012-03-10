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

public class UColumnInfo {
	private byte type;
	private byte collectionBaseType;
	private short scale;
	private int precision;
	private String[] enumValues;
	private String name, className, attributeName;
	// private String FQDN;
	private boolean isNullable;

	private String defaultValue;
	private byte is_auto_increment;
	private byte is_unique_key;
	private byte is_primary_key;
	private byte is_foreign_key;
	private byte is_reverse_index;
	private byte is_reverse_unique;
	private byte is_shared;

	UColumnInfo(byte cType, short cScale, int cPrecision, String cName) {
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

		defaultValue = null;
		is_auto_increment = 0;
		is_unique_key = 0;
		is_primary_key = 0;
		is_foreign_key = 0;
		is_reverse_index = 0;
		is_reverse_unique = 0;
		is_shared = 0;
	}

	/* get functions */
	public String getDefaultValue() {
		return defaultValue;
	}

	public byte getIsAutoIncrement() {
		return is_auto_increment;
	}

	public byte getIsUniqueKey() {
		return is_unique_key;
	}

	public byte getIsPrimaryKey() {
		return is_primary_key;
	}

	public byte getIsForeignKey() {
		return is_foreign_key;
	}

	public byte getIsReverseIndex() {
		return is_reverse_index;
	}

	public byte getIsReverseUnique() {
		return is_reverse_unique;
	}

	public byte getIsShared() {
		return is_shared;
	}

	public boolean isNullable() {
		return isNullable;
	}

	public String getClassName() {
		return className;
	}

	public int getCollectionBaseType() {
		return collectionBaseType;
	}

	public String getColumnName() {
		return name;
	}

	public int getColumnPrecision() {
		return precision;
	}

	public int getColumnScale() {
		return (int) scale;
	}

	public byte getColumnType() {
		return type;
	}

	public String getFQDN() {
		// return FQDN;
		return (findFQDN(type, precision, collectionBaseType));
	}

	public String getEnumValue (short index){
		if (enumValues == null || index - 1 < 0
				|| index - 1 > enumValues.length) {
			return null;
		}	
		return enumValues[index - 1];
	}

	public String getRealColumnName() {
		return attributeName;
	}

	static byte[] confirmType(byte originalType) {
		int collectionTypeOrNot = 0;
		byte typeInfo[];

		typeInfo = new byte[2];
		collectionTypeOrNot = originalType & (byte) 0140;
		switch (collectionTypeOrNot) {
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

	synchronized void setRemainedData(String aName, String cName, boolean hNull) {
		attributeName = aName;
		className = cName;
		isNullable = hNull;
	}

	/* set the extra fields */
	synchronized void setExtraData(String defValue, byte bAI, byte bUK,
			byte bPK, byte bFK, byte bRI, byte bRU, byte sh, String[] enumValues) {
		defaultValue = defValue;
		is_auto_increment = bAI;
		is_unique_key = bUK;
		is_primary_key = bPK;
		is_foreign_key = bFK;
		is_reverse_index = bRI;
		is_reverse_unique = bRU;
		is_shared = sh;
		if (enumValues != null) {
			this.enumValues = enumValues.clone();
		} else {
			this.enumValues = null;
		}
	}

	private String findFQDN(byte cType, int cPrecision, byte cBaseType) {
		switch (cType) {
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
			if (UJCIUtil.isMysqlMode(this.getClass())) {
				return "java.math.BigDecimal";
			}
			return "java.lang.Double";
		case UUType.U_TYPE_SHORT:
			return "java.lang.Short";
		case UUType.U_TYPE_INT:
			return "java.lang.Integer";
		case UUType.U_TYPE_BIGINT:
			return "java.lang.Long";
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
		case UUType.U_TYPE_DATETIME:
			return "java.sql.Timestamp";
		case UUType.U_TYPE_SET:
		case UUType.U_TYPE_SEQUENCE:
		case UUType.U_TYPE_MULTISET:
			break;
		case UUType.U_TYPE_OBJECT:
			return "cubrid.sql.CUBRIDOID";
		case UUType.U_TYPE_BLOB:
			return "java.sql.Blob";
		case UUType.U_TYPE_CLOB:
			return "java.sql.Clob";
		default:
			return "";
		}
		switch (cBaseType) {
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
		case UUType.U_TYPE_BIGINT:
			return "java.lang.Long[]";
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
		case UUType.U_TYPE_DATETIME:
			return "java.sql.Timestamp[]";
		case UUType.U_TYPE_SET:
		case UUType.U_TYPE_SEQUENCE:
		case UUType.U_TYPE_MULTISET:
			break;
		case UUType.U_TYPE_OBJECT:
			return "cubrid.sql.CUBRIDOID[]";
		case UUType.U_TYPE_BLOB:
			return "java.sql.Blob[]";
		case UUType.U_TYPE_CLOB:
			return "java.sql.Clob[]";
		default:
			break;
		}
		return null;
	}
}
