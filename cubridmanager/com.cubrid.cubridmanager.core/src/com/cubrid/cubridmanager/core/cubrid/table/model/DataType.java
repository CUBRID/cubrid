/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */
package com.cubrid.cubridmanager.core.cubrid.table.model;

import java.io.IOException;
import java.io.StringReader;
import java.text.ParseException;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import com.cubrid.cubridmanager.core.CommonTool;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.CSVReader;

/**
 * to provide methods and constants of data type in CUBRID
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-4 created by moulinwang
 */
public class DataType {
	private static String[][] typeMapping = { { "SMALLINT", "smallint" },
			{ "INTEGER", "integer" }, { "BIGINT", "bigint" },
			{ "NUMERIC", "numeric" }, { "FLOAT", "float" },
			{ "DOUBLE", "double" }, { "CHAR", "character" },
			{ "VARCHAR", "character varying" },
			{ "NCHAR", "national character" },
			{ "NCHAR VARYING", "national character varying" },
			{ "TIME", "time" }, { "DATE", "date" },
			{ "TIMESTAMP", "timestamp" }, { "DATETIME", "datetime" },
			{ "BIT", "bit" }, { "BIT VARYING", "bit varying" },
			{ "MONETARY", "monetary" }, { "STRING", "string" },
			{ "SET", "set_of" }, { "MULTISET", "multiset_of" },
			{ "SEQUENCE", "sequence_of" }, { "OBJECT", "object" } };
	
	private static final String DATETIME_FORMAT = "yyyy-MM-dd HH:mm:ss.SSS";

	/**
	 * return whether a given type is basic data type in CUBRID
	 * 
	 * @param type
	 * @return
	 */
	public static boolean isBasicType(String type) {
		String typePart = DataType.getTypePart(type);
		for (String[] item : typeMapping) {
			if (typePart.equals(item[1])) {
				return true;
			}
		}
		return true;
	}

	/**
	 * generate data type via JDBC meta data information of CUBRID
	 * 
	 * @param colType data type
	 * @param elemType element type in collection if colType is SET,MULTISET or
	 *        SEQUENCE
	 * @param precision valid digital number
	 * @param scale float digital number
	 * @return
	 */
	public static String makeType(String colType, String elemType,
			int precision, int scale) {
		if (colType.equals("SMALLINT") || colType.equals("INTEGER")
				|| colType.equals("BIGINT")

				|| colType.equals("FLOAT") || colType.equals("DOUBLE")
				|| colType.equals("MONETARY")

				|| colType.equals("TIME") || colType.equals("DATE")
				|| colType.equals("TIMESTAMP") || colType.equals("DATETIME")) {
			return colType;
		} else if (colType.equals("CHAR") || colType.equals("VARCHAR")
				|| colType.equals("NCHAR") || colType.equals("NCHAR VARYING")
				|| colType.equals("BIT") || colType.equals("BIT VARYING")) {
			return colType + "(" + precision + ")";
		} else if (colType.equals("NUMERIC")) {
			return colType + "(" + precision + "," + scale + ")";
		} else if (colType.equals("SET") || colType.equals("MULTISET")
				|| colType.equals("SEQUENCE")) {
			return colType + "(" + makeType(elemType, null, precision, scale)
					+ ")";
		}
		return colType;
	}

	/**
	 * return element type in collection if colType is SET,MULTISET or SEQUENCE
	 * else return null;
	 * 
	 * @param jdbcType
	 * @return
	 */
	public static String getElemType(String jdbcType) {
		String outType = getTypePart(jdbcType);
		if (outType.equals("SET") || outType.equals("MULTISET")
				|| outType.equals("SEQUENCE")) {
			String innerType = getTypeRemain(jdbcType);
			return getTypePart(innerType);
		} else {
			return null;
		}
	}

	/**
	 * return valid letter or digital number
	 * 
	 * @param jdbcType
	 * @return
	 */
	public static int getPrecision(String jdbcType) {
		String type = getTypePart(jdbcType);
		if (type.equals("SET") || type.equals("MULTISET")
				|| type.equals("SEQUENCE")) {
			type = getTypeRemain(jdbcType);
		}
		String typeRemain = getTypeRemain(type);
		if (null == typeRemain) {
			return -1;
		} else {
			int index = typeRemain.indexOf(",");
			if (index != -1) {
				return Integer.parseInt(typeRemain.substring(index + 1,
						typeRemain.length()));
			} else {
				return -1;
			}
		}
	}

	/**
	 * return float digital number
	 * 
	 * @param jdbcType
	 * @return
	 */
	public static int getScale(String jdbcType) {
		String type = getTypePart(jdbcType);
		if (type.equals("SET") || type.equals("MULTISET")
				|| type.equals("SEQUENCE")) {
			type = getTypeRemain(jdbcType);
		}
		String typeRemain = getTypeRemain(type);
		if (null == typeRemain) {
			return -1;
		} else {
			int index = typeRemain.indexOf(",");
			if (index != -1) {
				return Integer.parseInt(typeRemain.substring(0, index));
			} else {
				return Integer.parseInt(typeRemain);
			}
		}
	}

	/**
	 * get a list of super classes to a table
	 * 
	 * @param database
	 * @param table
	 * @return
	 */
	public static Set<String> getSuperClasses(DatabaseInfo database,
			String table) {
		Set<String> set = new HashSet<String>();
		List<String> supers = database.getSchemaInfo(table).getSuperClasses();
		set.addAll(supers);
		for (String sup : supers) {
			set.addAll(getSuperClasses(database, sup));
		}
		return set;
	}

	/**
	 * check whether two types are compatible in CUBRID
	 * 
	 * @param database
	 * @param type1
	 * @param type2
	 * @return
	 */
	public static Integer isCompatibleType(DatabaseInfo database, String type1,
			String type2) {
		if (type1.equals(type2)) {
			return 0;
		}
		if (DataType.isBasicType(type1) == DataType.isBasicType(type2)) {
			if (DataType.isBasicType(type1)) {
				if (type1.equals(type2)) {
					return 0;
				} else {
					return null;
				}
			} else {
				Set<String> set1 = getSuperClasses(database, type1);
				if (set1.contains(type2)) {
					return 1;
				}
				Set<String> set2 = getSuperClasses(database, type2);
				if (set2.contains(type1)) {
					return -1;
				}
				return null;
			}
		} else {
			return null;
		}
	}

	/**
	 * return the type part of a special data type,eg:
	 * <li> character(10), return "character"
	 * <li> set(integer), return "set"
	 * <li> integer, return "integer"
	 * 
	 * @param type
	 * @return
	 */
	public static String getTypePart(String type) {
		int index = type.indexOf("(");
		if (-1 == index) { //the simplest case
			return type;
		} else { //the case like: set_of(bit) ,numeric(4,2)
			return type.substring(0, index);
		}
	}

	/**
	 * return the remained part of a special data type,eg:
	 * <li> character(10), return "10"
	 * <li> set(integer), return "integer"
	 * <li> integer, return null
	 * 
	 * @see #getTypePart(String)
	 * 
	 * @param type
	 * @return
	 */
	public static String getTypeRemain(String type) {
		int index = type.indexOf("(");
		if (-1 == index) { //the simplest case
			return null;
		} else { //the case like: set_of(bit) ,numeric(4,2)
			return type.substring(index + 1, type.length() - 1);
		}
	}

	/**
	 * To change CUBRID type string to upper case and shorter CUBRID type should
	 * be shown short and upper case
	 * 
	 * @param type
	 * @return
	 */
	public static String getShownType(String type) {
		int index = type.indexOf("(");
		String typepart = null;
		String typedesc = null;

		if (-1 == index) { //the simplest case
			for (String[] item : typeMapping) {
				if (type.equals(item[1])) {
					return item[0];
				}
			}
		} else { //the case like: set_of(bit) ,numeric(4,2)
			typepart = type.substring(0, index);
			typedesc = type.substring(index + 1, type.length() - 1);
			for (String[] item : typeMapping) {
				if (typepart.equals(item[1])) {
					if (-1 == typepart.indexOf("_of")) {
						return item[0] + "(" + typedesc + ")";
					} else {
						return item[0] + "(" + getShownType(typedesc) + ")";
					}
				}
			}
		}
		return type;
	}

	/**
	 * To change upper case and shorter shown type string to CUBRID type CUBRID
	 * type should be shown short and upper case, sometime needing reverse cast.
	 * 
	 * @param type
	 * @return
	 */
	public static String getType(String shownType) {
		int index = shownType.indexOf("(");
		String typepart = null;
		String typedesc = null;

		if (-1 == index) { //the simplest case
			for (String[] item : typeMapping) {
				if (shownType.equals(item[0])) {
					return item[1];
				}
			}
		} else { //the case like: set_of(bit) ,numeric(4,2)
			typepart = shownType.substring(0, index);
			typedesc = shownType.substring(index + 1, shownType.length() - 1);
			for (String[] item : typeMapping) {
				if (typepart.equals(item[0])) {
					if (typepart.equals("SET") || typepart.equals("MULTISET")
							|| typepart.equals("SEQUENCE")) {
						return item[1] + "(" + getType(typedesc) + ")";
					} else {
						return item[1] + "(" + typedesc + ")";
					}
				}
			}
		}
		return shownType;
	}

	public static String[][] getTypeMapping() {
		return typeMapping;
	}

	/**
	 * return the max integer when the data domain is like Numeric(k,0)
	 * 
	 * @param digitalNum
	 * @return
	 */
	public static String getNumericMaxValue(int digitalNum) {
		assert (digitalNum > 0);
		StringBuffer bf = new StringBuffer();
		for (int i = 0; i < digitalNum; i++) {
			bf.append("9");
		}
		return bf.toString();
	}

	/**
	 * return the min integer when the data domain is like Numeric(k,0)
	 * 
	 * @param digitalNum
	 * @return
	 */
	public static String getNumericMinValue(int digitalNum) {
		assert (digitalNum > 0);
		StringBuffer bf = new StringBuffer();
		bf.append("-");
		for (int i = 0; i < digitalNum; i++) {
			bf.append("9");
		}
		return bf.toString();
	}

	/**
	 * return Object[] array value from a collection value based the given data
	 * type, eg: data type: integer, collection value: {1,2,3} return Object[]:
	 * Integer[]{1,2,3}
	 * 
	 * 
	 * @param type
	 * @param value
	 * @return
	 * @throws ParseException
	 */
	public static Object[] getCollectionValues(String type, String value) throws NumberFormatException,
			ParseException {
		String strs = value;
		String innerType = getTypeRemain(type);
		assert (innerType != null);
		if (value.startsWith("{") && value.endsWith("}")) {
			strs = value.substring(1, value.length() - 1);
		}
		CSVReader reader = new CSVReader(new StringReader(strs));
		String[] values = new String[0];
		try {
			values = reader.readNext();
			reader.close();
		} catch (IOException ignored) {
			//ignored
		}
		Object[] ret = null;
		if (innerType.equalsIgnoreCase("smallint")
				|| innerType.equalsIgnoreCase("integer")) {
			ret = new Integer[values.length];
		} else if (innerType.equalsIgnoreCase("bigint")) {
			ret = new Long[values.length];
		} else if (innerType.startsWith("numeric(")
				&& innerType.endsWith(",0)")) {
			ret = new Long[values.length];
		} else if (innerType.equalsIgnoreCase("float")) {
			ret = new Double[values.length];
		} else if (innerType.equalsIgnoreCase("double")
				|| innerType.equalsIgnoreCase("monetary")) {
			ret = new Double[values.length];
		} else if (innerType.startsWith("numeric(")
				&& !innerType.endsWith(",0)")) {
			ret = new Double[values.length];
		} else if (innerType.startsWith("character")
				|| innerType.equalsIgnoreCase("string")) {
			ret = new String[values.length];
		} else if (innerType.equalsIgnoreCase("time")) {
			ret = new java.sql.Time[values.length];
		} else if (innerType.equalsIgnoreCase("date")) {
			ret = new java.sql.Date[values.length];
		} else if (innerType.equalsIgnoreCase("timestamp")) {
			ret = new java.sql.Timestamp[values.length];
		} else if (innerType.equalsIgnoreCase("datetime")) {
			ret = new java.sql.Timestamp[values.length];
		} else if (innerType.startsWith("bit(")
				|| innerType.startsWith("bit varying(")) {
			ret = new String[values.length];
		} else if (innerType.startsWith("national character")) {
			ret = new String[values.length];
		} else {
			ret = new String[values.length];
		}

		for (int i = 0; i < values.length; i++) {
			if (innerType.equalsIgnoreCase("smallint")
					|| innerType.equalsIgnoreCase("integer")) {
				ret[i] = new Integer(values[i].trim());
			} else if (innerType.equalsIgnoreCase("bigint")) {
				ret[i] = new Long(values[i].trim());
			} else if (innerType.startsWith("numeric(")
					&& innerType.endsWith(",0)")) {
				ret[i] = new Long(values[i].trim());
			} else if (innerType.equalsIgnoreCase("float")) {
				ret[i] = new Double(values[i].trim());
			} else if (innerType.equalsIgnoreCase("double")
					|| innerType.equalsIgnoreCase("monetary")) {
				ret[i] = new Double(values[i].trim());
			} else if (innerType.startsWith("numeric(")
					&& !innerType.endsWith(",0)")) {
				ret[i] = new Double(values[i].trim());
			} else if (innerType.startsWith("character")
					|| innerType.equalsIgnoreCase("string")) {
				ret[i] = values[i];
			} else if (innerType.equalsIgnoreCase("time")) {
				ret[i] = java.sql.Time.valueOf(values[i].trim());
			} else if (innerType.equalsIgnoreCase("date")) {
				ret[i] = java.sql.Date.valueOf(values[i].trim());
			} else if (innerType.equalsIgnoreCase("timestamp")) {
				long time = CommonTool.getDatetime(values[i].trim());
				java.sql.Timestamp timestamp = new java.sql.Timestamp(time);
				ret[i] = timestamp;
			} else if (innerType.equalsIgnoreCase("datetime")) {
				String formatValue = CommonTool.formatDateTime(
						value, DATETIME_FORMAT);
				if (formatValue == null) {
					formatValue = value;
				}
				long time = CommonTool.getDatetime(formatValue);
				java.sql.Timestamp timestamp = new java.sql.Timestamp(time);
				ret[i] = timestamp;
			} else if (innerType.startsWith("bit(")
					|| innerType.startsWith("bit varying(")) {
				ret[i] = DBAttribute.formatValue(innerType, values[i].trim());
			} else if (innerType.startsWith("national character")) {
				ret[i] = values[i].trim();
			} else {
				ret[i] = values[i].trim();
			}
		}
		return ret;
	}
	
}
