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

import java.text.ParseException;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.CommonTool;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.cubrid.serial.model.SerialInfo;

/**
 * to store information of a column in the table schema
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-5 created by moulinwang
 */
public class DBAttribute implements
		Cloneable {
	private static final Logger logger = LogUtil.getLogger(DBAttribute.class);
	/**
	 * the standard format of time, date, timestamp and datetime used in SQL
	 */
	private static final String TIME_FORMAT = "HH:mm:ss";
	private static final String DATE_FORMAT = "MM/dd/yyyy";
	private static final String TIMESTAMP_FORMAT = "MM/dd/yyyy HH:mm:ss";
	private static final String DATETIME_FORMAT = "yyyy-MM-dd HH:mm:ss.SSS";
	/**
	 * Column Name
	 */
	private String name;
	private String type;
	private String inherit; // it belongs to which class
	private boolean indexed;
	private boolean notNull;
	private boolean shared;
	private boolean unique;
	private String defaultValue;
	private SerialInfo autoIncrement;
	private String domainClassName;
	private boolean isClassAttribute;

	@Override
	public boolean equals(Object obj) {
		if (this == obj) {
			return true;
		}
		if (obj == null) {
			return false;
		}
		if (!(obj instanceof DBAttribute)) {
			return false;
		}
		DBAttribute a = (DBAttribute) obj;
		boolean equal = a.name == null ? this.name == null
				: a.name.equals(this.name);
		equal = equal
				&& (a.type == null ? this.type == null
						: a.type.equals(this.type));
		equal = equal
				&& (a.inherit == null ? this.inherit == null
						: a.inherit.equals(this.inherit));
		equal = equal
				&& (a.defaultValue == null ? this.defaultValue == null
						: a.defaultValue.equals(this.defaultValue));
		equal = equal && (a.notNull == this.notNull);
		equal = equal && (a.indexed == this.indexed);
		equal = equal && (a.shared == this.shared);
		equal = equal && (a.unique == this.unique);

		equal = equal
				&& (a.autoIncrement == null ? this.autoIncrement == null
						: a.autoIncrement.equals(this.autoIncrement));

		return equal;
	}

	@Override
	public int hashCode() {
		return name.hashCode();
	}

	public DBAttribute clone() {
		DBAttribute newAttr = null;
		try {
			newAttr = (DBAttribute) super.clone();
		} catch (CloneNotSupportedException e) {
		}
		if (newAttr == null) {
			return null;
		}
		if (autoIncrement == null) {
			newAttr.autoIncrement = null;
		} else {
			newAttr.autoIncrement = autoIncrement.clone();
		}
		return newAttr;
	}

	public String getSharedValue() {
		return defaultValue;
	}

	public void setSharedValue(String sharedValue) {
		this.defaultValue = sharedValue;
		this.shared = true;
	}

	public String toString() {
		StringBuffer bf = new StringBuffer();
		bf.append("attribute name:" + this.name + "\n");
		bf.append("\tdata type:" + this.type + "\n");
		bf.append("\tinherit:" + this.inherit + "\n");
		bf.append("\tNot Null:" + this.notNull + "\n");
		bf.append("\tshared:" + this.shared + "\n");
		bf.append("\tunique:" + this.unique + "\n");
		bf.append("\tdefault:" + this.defaultValue + "\n");
		return bf.toString();
	}

	public DBAttribute(String name, String type, String inherit,
			boolean isIndexed, boolean isNotNull, boolean isShared,
			boolean isUnique, String defaultval) {
		this.name = name;
		this.type = type;
		this.inherit = inherit;
		this.indexed = isIndexed;
		this.notNull = isNotNull;
		this.shared = isShared;
		this.unique = isUnique;
		this.defaultValue = defaultval;
		resetDefault();
	}

	public DBAttribute() {
	}

	public String getName() {
		return name;
	}

	public void setName(String name) {
		this.name = name;
	}

	public String getType() {
		return type;
	}

	public void setType(String type) {
		this.type = type;
	}

	public String getInherit() {
		return inherit;
	}

	public void setInherit(String inherit) {
		this.inherit = inherit;
	}

	public boolean isIndexed() {
		return indexed;
	}

	public void setIndexed(boolean indexed) {
		this.indexed = indexed;
	}

	public boolean isNotNull() {
		return notNull;
	}

	public void setNotNull(boolean notNull) {
		this.notNull = notNull;
	}

	public boolean isShared() {
		return shared;
	}

	public void setShared(boolean shared) {
		this.shared = shared;
	}

	public boolean isUnique() {
		return unique;
	}

	public void setUnique(boolean unique) {
		this.unique = unique;
	}

	public String getDefault() {
		return defaultValue;
	}

	public void setDefault(String defaultValue) {
		this.defaultValue = defaultValue;
	}

	/**
	 * reset default value, for the default value from server client is not good
	 * for user, it should be changed to a given format
	 * 
	 */
	public void resetDefault() {
		if (defaultValue == null) {
			return;
		}
		if (type.equalsIgnoreCase("datetime")) {
			try {
				String value = defaultValue;
				if (value.startsWith("datetime")) {
					value = value.replace("datetime", "").trim();
				}
				if (value.startsWith("'") && value.endsWith("'")) {
					value = value.substring(1, value.length() - 1).trim();
				}
				String formatValue = CommonTool.formatDateTime(
						value, DATETIME_FORMAT);
				if (formatValue == null) {
					formatValue = value;
				}
				long datetime = CommonTool.getDatetime(formatValue);
				defaultValue = CommonTool.getDatetimeString(datetime,
						"yyyy/MM/dd a hh:mm:ss.SSS");
			} catch (ParseException e) {
				logger.error(e);
			}
		} else if (type.equalsIgnoreCase("timestamp")) {
			try {
				String value = defaultValue;
				if (value.startsWith("timestamp")) {
					value = value.replace("timestamp", "").trim();
				}
				if (value.startsWith("'") && value.endsWith("'")) {
					value = value.substring(1, value.length() - 1).trim();
				}
				long timestamp = CommonTool.getTimestamp(value);
				defaultValue = CommonTool.getTimestampString(timestamp,
						"yyyy/MM/dd a hh:mm:ss");
			} catch (ParseException e) {
				logger.error(e);
			}
		} else if (type.equalsIgnoreCase("date")) {
			try {
				String value = defaultValue;
				if (value.startsWith("date")) {
					value = value.replace("date", "").trim();
				}
				if (value.startsWith("'") && value.endsWith("'")) {
					value = value.substring(1, value.length() - 1).trim();
				}
				long timestamp = CommonTool.getDate(value);
				defaultValue = CommonTool.getTimestampString(timestamp,
						"yyyy/MM/dd");
			} catch (ParseException e) {
				logger.error(e);
			}
		} else if (type.equalsIgnoreCase("time")) {
			try {
				String value = defaultValue;
				if (value.startsWith("time")) {
					value = value.replace("time", "").trim();
				}
				if (value.startsWith("'") && value.endsWith("'")) {
					value = value.substring(1, value.length() - 1).trim();
				}
				long timestamp = CommonTool.getTime(value);
				defaultValue = CommonTool.getTimestampString(timestamp,
						"a hh:mm:ss");
			} catch (ParseException e) {
				logger.error(e);
			}
		} else if (type.toLowerCase().startsWith("char")
				|| type.equalsIgnoreCase("string")) { // include character
			// and character
			// varying
			if (defaultValue == null || defaultValue.equals("")) {

			} else if (defaultValue.startsWith("'")
					&& defaultValue.endsWith("'") && defaultValue.length() > 1) {
				defaultValue = defaultValue.substring(1,
						defaultValue.length() - 1).replace("''", "'");
			} else {
				//				defaultValue = "'" + defaultValue + "'";
			}
		}
		if (type.toLowerCase().startsWith("national")) { // include national
			// character and
			// national
			// character varying
			// TODO
			// do nothing for the server client does not support bit and nchar
		} else if (type.toLowerCase().startsWith("bit")) { // include bit and
			// bit varying
			// TODO
			// do nothing for the server client does not support bit and nchar
		} else if (type.toUpperCase().startsWith("NUMERIC")
				|| type.toUpperCase().startsWith("INTEGER")
				|| type.toUpperCase().startsWith("SMALLINT")
				|| type.toUpperCase().startsWith("FLOAT")
				|| type.toUpperCase().startsWith("DOUBLE")
				|| type.toUpperCase().startsWith("MONETARY")) {
			// TODO
			// do nothing for it is pretty good.
		}
	}

	public static String formatValue(String atttype, String attrValue) throws NumberFormatException,
			ParseException {
		FormatDataResult result = format(atttype, attrValue);
		return result.formatResult;
	}

	/**
	 * try to validate whether attribute value is aligned with the given data
	 * type
	 */
	public static boolean validateAttributeValue(String atttype,
			String attrValue) {
		FormatDataResult result = null;
		try {
			result = format(atttype, attrValue);
			return result.success;
		} catch (NumberFormatException e) {
			return false;
		} catch (ParseException e) {
			return false;
		}

	}

	/**
	 * to format customs' many types of attribute default value into standard
	 * attribute default value
	 * 
	 * @param atttype String attribute type
	 * @param attrValue String attribute default value
	 * @return String standard attribute default value
	 * @throws ParseException
	 */
	public static FormatDataResult format(String atttype, String attrValue) throws NumberFormatException,
			ParseException {
		FormatDataResult result = new FormatDataResult();
		if (atttype.equalsIgnoreCase("datetime")) {
			if (attrValue.equals("")) {
				result.success = true;
				result.formatResult = attrValue;
				return result;
			} else if (attrValue.equalsIgnoreCase("sysdatetime")
					|| attrValue.equalsIgnoreCase("sys_datetime")) {
				result.success = true;
				result.formatResult = "sysdatetime";
				return result;
			} else if (attrValue.equalsIgnoreCase("current_datetime")
					|| attrValue.equalsIgnoreCase("current_datetime")) {
				result.success = true;
				result.formatResult = "current_datetime";
				return result;
			} else if (attrValue.trim().toLowerCase().startsWith("datetime")) {
				String str = attrValue.trim().substring("datetime".length()).trim();
				if (str.startsWith("'") && str.endsWith("'")
						&& str.length() > 2) {
					result.success = true;
					result.formatResult = attrValue;
					return result;
				}
			} else {
				try {
					Long.parseLong(attrValue);
					result.success = true;
					result.formatResult = attrValue;
					return result;
				} catch (NumberFormatException nfe) {
					try {
						String formatValue = CommonTool.formatDateTime(
								attrValue, DATETIME_FORMAT);
						if (formatValue == null) {
							formatValue = attrValue;
						}
						long datetime = CommonTool.getDatetime(formatValue);
						result.success = true;
						result.formatResult = "DATETIME'"
								+ CommonTool.getDatetimeString(datetime,
										DATETIME_FORMAT) + "'";
						return result;
					} catch (ParseException e) {
						if (CommonTool.validateTimestamp(attrValue,
								"HH:mm:ss mm/dd")
								|| CommonTool.validateTimestamp(attrValue,
										"mm/dd HH:mm:ss")
								|| CommonTool.validateTimestamp(attrValue,
										"hh:mm:ss a mm/dd")
								|| CommonTool.validateTimestamp(attrValue,
										"mm/dd hh:mm:ss a")) {
							result.success = true;
							result.formatResult = "DATETIME'" + attrValue + "'";
							return result;
						} else {
							throw e;
						}
					}
				}
			}
		} else if (atttype.equalsIgnoreCase("timestamp")) {
			if (attrValue.equals("")) {
				result.success = true;
				result.formatResult = attrValue;
				return result;
			} else if (attrValue.equalsIgnoreCase("systimestamp")
					|| attrValue.equalsIgnoreCase("sys_timestamp")) {
				result.success = true;
				result.formatResult = "systimestamp";
				return result;
			} else if (attrValue.equalsIgnoreCase("currenttimestamp")
					|| attrValue.equalsIgnoreCase("current_timestamp")) {
				result.success = true;
				result.formatResult = "current_timestamp";
				return result;
			} else if (attrValue.toLowerCase().startsWith("timestamp")) {
				String str = attrValue.trim().substring("timestamp".length()).trim();
				if (str.startsWith("'") && str.endsWith("'")
						&& str.length() > 2) {
					result.success = true;
					result.formatResult = attrValue;
					return result;
				}
			} else {
				try {
					Long.parseLong(attrValue);
					result.success = true;
					result.formatResult = attrValue;
					return result;
				} catch (NumberFormatException nfe) {
					try {
						long timestamp = CommonTool.getTimestamp(attrValue);
						result.success = true;
						result.formatResult = "TIMESTAMP'"
								+ CommonTool.getTimestampString(timestamp,
										TIMESTAMP_FORMAT) + "'";
						return result;
					} catch (ParseException e) {
						if (CommonTool.validateTimestamp(attrValue,
								"HH:mm:ss mm/dd")
								|| CommonTool.validateTimestamp(attrValue,
										"mm/dd HH:mm:ss")
								|| CommonTool.validateTimestamp(attrValue,
										"hh:mm:ss a mm/dd")
								|| CommonTool.validateTimestamp(attrValue,
										"mm/dd hh:mm:ss a")) {
							result.success = true;
							result.formatResult = "TIMESTAMP'" + attrValue
									+ "'";
							return result;
						} else {
							throw e;
						}
					}
				}
			}
		} else if (atttype.equalsIgnoreCase("date")) {
			if (attrValue.equals("")) {
				result.success = true;
				result.formatResult = attrValue;
				return result;
			} else if (attrValue.equalsIgnoreCase("sysdate")
					|| attrValue.equalsIgnoreCase("sys_date")) {
				result.success = true;
				result.formatResult = "sysdate";
				return result;
			} else if (attrValue.equalsIgnoreCase("currentdate")
					|| attrValue.equalsIgnoreCase("current_date")) {
				result.success = true;
				result.formatResult = "current_date";
				return result;
			} else if (attrValue.toLowerCase().startsWith("date")) {
				String str = attrValue.trim().substring("date".length()).trim();
				if (str.startsWith("'") && str.endsWith("'")
						&& str.length() > 2) {
					result.success = true;
					result.formatResult = attrValue;
					return result;
				}
			} else {
				try {
					long timestamp = CommonTool.getDate(attrValue);
					result.success = true;
					result.formatResult = "DATE'"
							+ CommonTool.getTimestampString(timestamp,
									DATE_FORMAT) + "'";
					return result;
				} catch (ParseException e) {
					if (CommonTool.validateTimestamp(attrValue, "MM/dd")) {
						result.success = true;
						result.formatResult = "DATE'" + attrValue + "'";
						return result;
					} else {
						throw e;
					}
				}

			}
		} else if (atttype.equalsIgnoreCase("time")) {
			if (attrValue.equals("")) {
				result.success = true;
				result.formatResult = attrValue;
				return result;
			} else if (attrValue.equalsIgnoreCase("systime")
					|| attrValue.equalsIgnoreCase("sys_time")) {
				result.success = true;
				result.formatResult = "systime";
				return result;
			} else if (attrValue.equalsIgnoreCase("currenttime")
					|| attrValue.equalsIgnoreCase("current_time")) {
				result.success = true;
				result.formatResult = "current_time";
				return result;
			} else if (attrValue.toLowerCase().startsWith("time")) {
				String str = attrValue.trim().substring("time".length()).trim();
				if (str.startsWith("'") && str.endsWith("'")
						&& str.length() > 2) {
					result.success = true;
					result.formatResult = attrValue;
					return result;
				}
			} else {
				try {
					Long.parseLong(attrValue);
					result.success = true;
					result.formatResult = attrValue;
					return result;
				} catch (NumberFormatException nfe) {
					long timestamp = CommonTool.getTime(attrValue);
					result.success = true;
					result.formatResult = "TIME'"
							+ CommonTool.getTimestampString(timestamp,
									TIME_FORMAT) + "'";
					return result;
				}
			}
		} else if (atttype.toLowerCase().startsWith("char")
				|| atttype.toLowerCase().startsWith("varchar")
				|| atttype.equalsIgnoreCase("string")) {
			if (attrValue.startsWith("'") && attrValue.endsWith("'")
					&& attrValue.length() > 1 || attrValue.equals("")) {
				result.success = true;
				result.formatResult = attrValue;
				return result;
			} else {
				result.success = true;
				result.formatResult = "'" + attrValue.replaceAll("'", "''")
						+ "'";
				return result;
			}
		} else if (atttype.toLowerCase().startsWith("integer")
				|| atttype.toLowerCase().startsWith("smallint")
				|| atttype.equalsIgnoreCase("bigint")
				|| atttype.toLowerCase().startsWith("numeric")
				&& -1 != atttype.indexOf(",0)")) {
			Long.parseLong(attrValue);
			result.success = true;
			result.formatResult = attrValue;
			return result;
		} else if (atttype.toLowerCase().startsWith("numeric")
				&& -1 == atttype.indexOf(",0)")
				|| atttype.toLowerCase().startsWith("float")
				|| atttype.equalsIgnoreCase("double")
				|| atttype.toLowerCase().startsWith("monetary")) {
			Double.parseDouble(attrValue);
			result.success = true;
			result.formatResult = attrValue;
			return result;
		} else if (atttype.toLowerCase().startsWith("national character")
				|| atttype.toLowerCase().startsWith(
						"national character varying")) {
			if (attrValue.startsWith("N'") && attrValue.endsWith("'")
					|| attrValue.equals("")) {
				result.success = true;
				result.formatResult = attrValue;
				return result;
			} else {
				result.success = true;
				result.formatResult = "N'" + attrValue.replaceAll("'", "''")
						+ "'";
				return result;
			}
		} else if (atttype.toLowerCase().startsWith("bit")
				|| atttype.toLowerCase().startsWith("bit varying")) {
			if (attrValue.startsWith("B'") && attrValue.endsWith("'")
					|| attrValue.startsWith("X'") && attrValue.endsWith("'")
					|| attrValue.equals("")) {
				result.success = true;
				result.formatResult = attrValue;
				return result;
			} else {
				result.success = true;
				result.formatResult = "X'" + attrValue + "'";
				return result;
			}
		} else if (atttype.toLowerCase().startsWith("set_of")
				|| atttype.toLowerCase().startsWith("multiset_of")
				|| atttype.toLowerCase().startsWith("sequence_of")) {
			if (attrValue.startsWith("{") && attrValue.endsWith("}")
					|| attrValue.equals("")) {
				result.success = true;
				result.formatResult = attrValue;
				return result;
			} else {
				// assert every set/multiset/sequence has only type
				assert (-1 == atttype.indexOf(","));
				int index = atttype.indexOf("(");
				String subtype = atttype.substring(index + 1,
						atttype.length() - 1);
				StringBuffer bf = new StringBuffer();
				if (-1 == attrValue.indexOf(",")) {
					bf.append(formatValue(subtype, attrValue));
				} else {
					String[] values = attrValue.split(",");
					for (int j = 0; j < values.length; j++) {
						String value = values[j];
						if (j > 0) {
							bf.append(",");
						}
						bf.append(formatValue(subtype, value));
					}
				}
				result.success = true;
				result.formatResult = "{" + bf.toString() + "}";
				return result;
			}
		}
		result.success = false;
		result.formatResult = attrValue;
		return result;
	}

	public SerialInfo getAutoIncrement() {
		return autoIncrement;
	}

	public void setAutoIncrement(SerialInfo autoIncrement) {
		this.autoIncrement = autoIncrement;
	}

	public String getDomainClassName() {
		return domainClassName;
	}

	public void setDomainClassName(String domainClassName) {
		this.domainClassName = domainClassName;
	}

	public boolean isClassAttribute() {
		return isClassAttribute;
	}

	public void setClassAttribute(boolean isClassAttribute) {
		this.isClassAttribute = isClassAttribute;
	}
}

class FormatDataResult {
	String formatResult;
	boolean success;
}
