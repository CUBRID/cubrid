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

package cubrid.jdbc.driver;

import java.sql.ResultSetMetaData;
import java.sql.SQLException;

import cubrid.jdbc.jci.UColumnInfo;
import cubrid.jdbc.jci.UJCIUtil;
import cubrid.jdbc.jci.UUType;

/**
 * Title: CUBRID JDBC Driver Description:
 * 
 * @version 2.0
 */

public class CUBRIDResultSetMetaData implements ResultSetMetaData {
	private String[] col_name;
	private int[] col_type;
	private int[] ele_type;
	private String[] col_type_name;
	private String[] ele_type_name;
	private int[] col_prec;
	private int[] col_disp_size;
	private int[] col_scale;
	private String[] col_table;
	private int[] col_null;
	private String[] col_class_name;
	private boolean[] is_auto_increment_col;

	protected CUBRIDResultSetMetaData(UColumnInfo[] col_info) {
		col_name = new String[col_info.length];
		col_type = new int[col_info.length];
		ele_type = new int[col_info.length];
		col_type_name = new String[col_info.length];
		ele_type_name = new String[col_info.length];
		col_prec = new int[col_info.length];
		col_disp_size = new int[col_info.length];
		col_scale = new int[col_info.length];
		col_table = new String[col_info.length];
		col_null = new int[col_info.length];
		col_class_name = new String[col_info.length];
		is_auto_increment_col = new boolean[col_info.length];

		for (int i = 0; i < col_info.length; i++) {
			col_disp_size[i] = getDefaultColumnDisplaySize(col_info[i]
					.getColumnType());
			col_name[i] = col_info[i].getColumnName();
			col_prec[i] = col_info[i].getColumnPrecision();
			col_scale[i] = col_info[i].getColumnScale();
			col_table[i] = col_info[i].getClassName();
			col_type_name[i] = null;
			col_class_name[i] = col_info[i].getFQDN();
			if (col_info[i].isNullable())
				col_null[i] = columnNullable;
			else
				col_null[i] = columnNoNulls;

			if (col_info[i].getIsAutoIncrement() == 0) {
				is_auto_increment_col[i] = false;
			} else {
				is_auto_increment_col[i] = true;
			}

			switch (col_info[i].getColumnType()) {
			case UUType.U_TYPE_CHAR:
				col_type_name[i] = "CHAR";
				col_type[i] = java.sql.Types.CHAR;
				ele_type[i] = -1;
				if (col_prec[i] > col_disp_size[i]) {
					col_disp_size[i] = col_prec[i];
				}
				break;

			case UUType.U_TYPE_VARCHAR:
				col_type_name[i] = "VARCHAR";
				col_type[i] = java.sql.Types.VARCHAR;
				ele_type[i] = -1;
				if (col_prec[i] > col_disp_size[i]) {
					col_disp_size[i] = col_prec[i];
				}
				break;

			case UUType.U_TYPE_ENUM:
				col_type_name[i] = "ENUM";
				col_type[i] = java.sql.Types.VARCHAR;
				ele_type[i] = -1;
				if (col_prec[i] > col_disp_size[i]) {
					col_disp_size[i] = col_prec[i];
				}
				break;

			case UUType.U_TYPE_BIT:
				if (col_prec[i] == 8) {
					col_type_name[i] = "BIT";
					col_type[i] = java.sql.Types.BIT;
					ele_type[i] = -1;
				} else {
					col_type_name[i] = "BIT";
					col_type[i] = java.sql.Types.BINARY;
					ele_type[i] = -1;
				}
				if (col_prec[i] > col_disp_size[i]) {
					col_disp_size[i] = col_prec[i];
				}
				break;

			case UUType.U_TYPE_VARBIT:
				col_type_name[i] = "BIT VARYING";
				col_type[i] = java.sql.Types.VARBINARY;
				ele_type[i] = -1;
				if (col_prec[i] > col_disp_size[i]) {
					col_disp_size[i] = col_prec[i];
				}
				break;

			case UUType.U_TYPE_SHORT:
				if (UJCIUtil.isMysqlMode(this.getClass())) {
					col_disp_size[i] = col_prec[i];
				}
				col_type_name[i] = "SMALLINT";
				col_type[i] = java.sql.Types.SMALLINT;
				ele_type[i] = -1;
				break;

			case UUType.U_TYPE_INT:
				if (UJCIUtil.isMysqlMode(this.getClass())) {
					col_type_name[i] = "INT";
					col_disp_size[i] = col_prec[i];
				} else {
					col_type_name[i] = "INTEGER";
				}
				col_type[i] = java.sql.Types.INTEGER;
				ele_type[i] = -1;
				break;

			case UUType.U_TYPE_BIGINT:
				col_type_name[i] = "BIGINT";
				col_type[i] = java.sql.Types.BIGINT;
				ele_type[i] = -1;
				break;

			case UUType.U_TYPE_FLOAT:
				col_type_name[i] = "FLOAT";
				col_type[i] = java.sql.Types.REAL;
				ele_type[i] = -1;
				break;

			case UUType.U_TYPE_DOUBLE:
				col_type_name[i] = "DOUBLE";
				col_type[i] = java.sql.Types.DOUBLE;
				ele_type[i] = -1;
				break;

			case UUType.U_TYPE_NUMERIC:
				if (UJCIUtil.isMysqlMode(this.getClass())) {
					col_type_name[i] = "DECIMAL";
					col_type[i] = java.sql.Types.DECIMAL;
					col_disp_size[i] = col_prec[i] + 1;
					if (col_scale[i] > 0)
						col_disp_size[i] += 1;
					ele_type[i] = -1;
				} else {
					col_type_name[i] = "NUMERIC";
					col_type[i] = java.sql.Types.NUMERIC;
					ele_type[i] = -1;
				}
				break;

			case UUType.U_TYPE_MONETARY:
				col_type_name[i] = "MONETARY";
				col_type[i] = java.sql.Types.DOUBLE;
				ele_type[i] = -1;
				break;

			case UUType.U_TYPE_DATE:
				col_type_name[i] = "DATE";
				col_type[i] = java.sql.Types.DATE;
				ele_type[i] = -1;
				break;

			case UUType.U_TYPE_TIME:
				col_type_name[i] = "TIME";
				col_type[i] = java.sql.Types.TIME;
				ele_type[i] = -1;
				break;

			case UUType.U_TYPE_TIMESTAMP:
				col_type_name[i] = "TIMESTAMP";
				col_type[i] = java.sql.Types.TIMESTAMP;
				ele_type[i] = -1;
				break;

			case UUType.U_TYPE_DATETIME:
				col_type_name[i] = "DATETIME";
				col_type[i] = java.sql.Types.TIMESTAMP;
				ele_type[i] = -1;
				break;

			case UUType.U_TYPE_NULL:
				col_type_name[i] = "";
				// col_type[i] = java.sql.Types.NULL;
				col_type[i] = java.sql.Types.OTHER;
				ele_type[i] = -1;
				break;

			case UUType.U_TYPE_OBJECT:
				col_type_name[i] = "CLASS";
				col_type[i] = java.sql.Types.OTHER;
				ele_type[i] = -1;
				break;

			case UUType.U_TYPE_SET:
				col_type_name[i] = "SET";

			case UUType.U_TYPE_MULTISET:
				if (col_type_name[i] == null) {
					col_type_name[i] = "MULTISET";
				}

			case UUType.U_TYPE_SEQUENCE:
				if (col_type_name[i] == null) {
					col_type_name[i] = "SEQUENCE";
				}
				col_type[i] = java.sql.Types.OTHER;

				switch (col_info[i].getCollectionBaseType()) {
				case UUType.U_TYPE_CHAR:
					ele_type[i] = java.sql.Types.CHAR;
					ele_type_name[i] = "CHAR";
					break;
				case UUType.U_TYPE_VARCHAR:
					ele_type[i] = java.sql.Types.VARCHAR;
					ele_type_name[i] = "VARCHAR";
					break;
				case UUType.U_TYPE_ENUM:
					ele_type[i] = java.sql.Types.VARCHAR;
					ele_type_name[i] = "ENUM";
					break;
				case UUType.U_TYPE_BIT:
					if (col_info[i].getColumnPrecision() == 8) {
						ele_type[i] = java.sql.Types.BIT;
						ele_type_name[i] = "BIT";
					} else {
						ele_type[i] = java.sql.Types.BINARY;
						ele_type_name[i] = "BIT";
					}
					break;
				case UUType.U_TYPE_VARBIT:
					ele_type[i] = java.sql.Types.VARBINARY;
					ele_type_name[i] = "BIT VARYING";
					break;
				case UUType.U_TYPE_SHORT:
					ele_type[i] = java.sql.Types.SMALLINT;
					ele_type_name[i] = "SMALLINT";
					break;
				case UUType.U_TYPE_INT:
					ele_type[i] = java.sql.Types.INTEGER;
					ele_type_name[i] = "INTEGER";
					break;
				case UUType.U_TYPE_BIGINT:
					ele_type[i] = java.sql.Types.BIGINT;
					ele_type_name[i] = "BIGINT";
					break;
				case UUType.U_TYPE_FLOAT:
					ele_type[i] = java.sql.Types.REAL;
					ele_type_name[i] = "FLOAT";
					break;
				case UUType.U_TYPE_DOUBLE:
					ele_type[i] = java.sql.Types.DOUBLE;
					ele_type_name[i] = "DOUBLE";
					break;
				case UUType.U_TYPE_NUMERIC:
					ele_type[i] = java.sql.Types.NUMERIC;
					ele_type_name[i] = "NUMERIC";
					break;
				case UUType.U_TYPE_MONETARY:
					ele_type[i] = java.sql.Types.DOUBLE;
					ele_type_name[i] = "MONETARY";
					break;
				case UUType.U_TYPE_DATE:
					ele_type[i] = java.sql.Types.DATE;
					ele_type_name[i] = "DATE";
					break;
				case UUType.U_TYPE_TIME:
					ele_type[i] = java.sql.Types.TIME;
					ele_type_name[i] = "TIME";
					break;
				case UUType.U_TYPE_TIMESTAMP:
					ele_type[i] = java.sql.Types.TIMESTAMP;
					ele_type_name[i] = "TIMESTAMP";
					break;
				case UUType.U_TYPE_DATETIME:
					ele_type[i] = java.sql.Types.TIMESTAMP;
					ele_type_name[i] = "DATETIME";
					break;
				case UUType.U_TYPE_NULL:
					ele_type[i] = java.sql.Types.NULL;
					ele_type_name[i] = "";
					break;
				case UUType.U_TYPE_OBJECT:
					ele_type[i] = java.sql.Types.OTHER;
					ele_type_name[i] = "CLASS";
					break;
				case UUType.U_TYPE_SET:
					ele_type[i] = java.sql.Types.OTHER;
					ele_type_name[i] = "SET";
					break;
				case UUType.U_TYPE_MULTISET:
					ele_type[i] = java.sql.Types.OTHER;
					ele_type_name[i] = "MULTISET";
					break;
				case UUType.U_TYPE_SEQUENCE:
					ele_type[i] = java.sql.Types.OTHER;
					ele_type_name[i] = "SEQUENCE";
					break;
				case UUType.U_TYPE_NCHAR:
					ele_type[i] = java.sql.Types.CHAR;
					ele_type_name[i] = "NCHAR";
					break;
				case UUType.U_TYPE_VARNCHAR:
					ele_type[i] = java.sql.Types.VARCHAR;
					ele_type_name[i] = "NCHAR VARYING";
					break;
				default:
					break;
				}

				break;

			case UUType.U_TYPE_NCHAR:
				col_type_name[i] = "NCHAR";
				col_type[i] = java.sql.Types.CHAR;
				ele_type[i] = -1;
				if (col_prec[i] > col_disp_size[i]) {
					col_disp_size[i] = col_prec[i];
				}
				break;

			case UUType.U_TYPE_VARNCHAR:
				col_type_name[i] = "NCHAR VARYING";
				col_type[i] = java.sql.Types.VARCHAR;
				ele_type[i] = -1;
				if (col_prec[i] > col_disp_size[i]) {
					col_disp_size[i] = col_prec[i];
				}
				break;

			case UUType.U_TYPE_BLOB:
				col_type_name[i] = "BLOB";
				col_type[i] = java.sql.Types.BLOB;
				ele_type[i] = -1;
				break;

			case UUType.U_TYPE_CLOB:
				col_type_name[i] = "CLOB";
				col_type[i] = java.sql.Types.CLOB;
				ele_type[i] = -1;
				break;

			default:
				break;
			}
		}
	}

	CUBRIDResultSetMetaData(CUBRIDResultSetWithoutQuery r) {
		col_name = r.column_name;
		col_type = new int[col_name.length];
		ele_type = new int[col_name.length];
		col_type_name = new String[col_name.length];
		col_prec = new int[col_name.length];
		col_disp_size = new int[col_name.length];
		col_scale = new int[col_name.length];
		col_table = new String[col_name.length];
		col_null = new int[col_name.length];
		col_class_name = new String[col_name.length];

		for (int i = 0; i < col_name.length; i++) {
			col_disp_size[i] = getDefaultColumnDisplaySize((byte) r.type[i]);
			if (r.type[i] == UUType.U_TYPE_BIT) {
				col_type[i] = java.sql.Types.BIT;
				col_type_name[i] = "BIT";
				col_prec[i] = 1;
				col_class_name[i] = "byte[]";
			}
			if (r.type[i] == UUType.U_TYPE_INT) {
				col_type[i] = java.sql.Types.INTEGER;
				col_type_name[i] = "INTEGER";
				col_prec[i] = 10;
				col_class_name[i] = "java.lang.Integer";
			}
			if (r.type[i] == UUType.U_TYPE_SHORT) {
				col_type[i] = java.sql.Types.SMALLINT;
				col_type_name[i] = "SMALLINT";
				col_prec[i] = 5;
				col_class_name[i] = "java.lang.Short";
			}
			if (r.type[i] == UUType.U_TYPE_VARCHAR) {
				col_type[i] = java.sql.Types.VARCHAR;
				col_type_name[i] = "VARCHAR";
				col_prec[i] = r.precision[i];
				if (col_prec[i] > col_disp_size[i]) {
					col_disp_size[i] = col_prec[i];
				}
				col_class_name[i] = "java.lang.String";
			}
			if (r.type[i] == UUType.U_TYPE_ENUM) {
				col_type[i] = java.sql.Types.VARCHAR;
				col_type_name[i] = "ENUM";
				col_prec[i] = r.precision[i];
				if (col_prec[i] > col_disp_size[i]) {
					col_disp_size[i] = col_prec[i];
				}
				col_class_name[i] = "java.lang.String";
			}
			if (r.type[i] == UUType.U_TYPE_NULL) {
				col_type[i] = java.sql.Types.NULL;
				col_type_name[i] = "";
				col_prec[i] = 0;
				col_class_name[i] = "";
			}
			col_scale[i] = 0;
			ele_type[i] = -1;
			col_table[i] = "";
			if (r.nullable[i]) {
				col_null[i] = columnNullable;
			} else {
				col_null[i] = columnNoNulls;
			}
		}
	}

	private int getDefaultColumnDisplaySize(byte type) {
		/* return default column display size based on column type */
		int ret_size = -1;

		switch (type) {
		case UUType.U_TYPE_CHAR:
			ret_size = 1;
			break;
		case UUType.U_TYPE_VARCHAR:
		case UUType.U_TYPE_ENUM:
			ret_size = 1;
			break;
		case UUType.U_TYPE_BIT:
			ret_size = 1;
			break;
		case UUType.U_TYPE_VARBIT:
			ret_size = 1;
			break;
		case UUType.U_TYPE_SHORT:
			ret_size = 6;
			break;
		case UUType.U_TYPE_INT:
			ret_size = 11;
			break;
		case UUType.U_TYPE_BIGINT:
			ret_size = 20;
			break;
		case UUType.U_TYPE_FLOAT:
			ret_size = 13;
			break;
		case UUType.U_TYPE_DOUBLE:
			ret_size = 23;
			break;
		case UUType.U_TYPE_NUMERIC:
			ret_size = 40;
			break;
		case UUType.U_TYPE_MONETARY:
			ret_size = 23;
			break;
		case UUType.U_TYPE_DATE:
			ret_size = 10;
			break;
		case UUType.U_TYPE_TIME:
			ret_size = 8;
			break;
		case UUType.U_TYPE_TIMESTAMP:
			ret_size = 19;
			break;
		case UUType.U_TYPE_DATETIME:
			/* FIXME: as is 2010-11-11 20:10:10,to be 2010-11-11 20:20:20.123 */
			ret_size = 19;
			break;
		case UUType.U_TYPE_NULL:
			ret_size = 4;
			break;
		case UUType.U_TYPE_OBJECT:
			ret_size = 256;
			break;
		case UUType.U_TYPE_SET:
		case UUType.U_TYPE_MULTISET:
		case UUType.U_TYPE_SEQUENCE:
			ret_size = -1;
			break;
		case UUType.U_TYPE_NCHAR:
			ret_size = 1;
			break;
		case UUType.U_TYPE_VARNCHAR:
			ret_size = 1;
			break;
		default:
			break;
		}
		return ret_size;
	}

	/*
	 * java.sql.ResultSetMetaData interface
	 */

	public int getColumnCount() throws SQLException {
		return col_name.length;
	}

	public boolean isAutoIncrement(int column) throws SQLException {
		checkColumnIndex(column);
		
		return is_auto_increment_col[column - 1];
	}

	public boolean isCaseSensitive(int column) throws SQLException {
		checkColumnIndex(column);

		if (col_type[column - 1] == java.sql.Types.CHAR
				|| col_type[column - 1] == java.sql.Types.VARCHAR
				|| col_type[column - 1] == java.sql.Types.LONGVARCHAR) {
			return true;
		}

		return false;
	}

	public boolean isSearchable(int column) throws SQLException {
		checkColumnIndex(column);
		return true;
	}

	public boolean isCurrency(int column) throws SQLException {
		checkColumnIndex(column);

		if (UJCIUtil.isMysqlMode(this.getClass())) {
			return false;
		}

		if (col_type[column - 1] == java.sql.Types.DOUBLE
				|| col_type[column - 1] == java.sql.Types.REAL
				|| col_type[column - 1] == java.sql.Types.NUMERIC) {
			return true;
		}

		return false;
	}

	public int isNullable(int column) throws SQLException {
		checkColumnIndex(column);
		return col_null[column - 1];
	}

	public boolean isSigned(int column) throws SQLException {
		checkColumnIndex(column);

		if (col_type[column - 1] == java.sql.Types.SMALLINT
				|| col_type[column - 1] == java.sql.Types.INTEGER
				|| col_type[column - 1] == java.sql.Types.NUMERIC
				|| col_type[column - 1] == java.sql.Types.DECIMAL
				|| col_type[column - 1] == java.sql.Types.REAL
				|| col_type[column - 1] == java.sql.Types.DOUBLE) {
			return true;
		}

		return false;
	}

	public int getColumnDisplaySize(int column) throws SQLException {
		checkColumnIndex(column);
		return col_disp_size[column - 1];
	}

	public String getColumnLabel(int column) throws SQLException {
		checkColumnIndex(column);
		return getColumnName(column);
	}

	public String getColumnName(int column) throws SQLException {
		checkColumnIndex(column);
		return col_name[column - 1];
	}

	public String getSchemaName(int column) throws SQLException {
		checkColumnIndex(column);
		return "";
	}

	public int getPrecision(int column) throws SQLException {
		checkColumnIndex(column);
		return col_prec[column - 1];
	}

	public int getScale(int column) throws SQLException {
		checkColumnIndex(column);
		return col_scale[column - 1];
	}

	public String getTableName(int column) throws SQLException {
		checkColumnIndex(column);
		return col_table[column - 1];
	}

	public String getCatalogName(int column) throws SQLException {
		checkColumnIndex(column);
		return "";
	}

	public int getColumnType(int column) throws SQLException {
		checkColumnIndex(column);
		return col_type[column - 1];
	}

	public String getColumnTypeName(int column) throws SQLException {
		checkColumnIndex(column);
		return col_type_name[column - 1];
	}

	public boolean isReadOnly(int column) throws SQLException {
		checkColumnIndex(column);
		return false;
	}

	public boolean isWritable(int column) throws SQLException {
		checkColumnIndex(column);
		return true;
	}

	public boolean isDefinitelyWritable(int column) throws SQLException {
		checkColumnIndex(column);
		return false;
	}

	public String getColumnClassName(int column) throws SQLException {
		checkColumnIndex(column);
		return col_class_name[column - 1];
	}

	public int getElementType(int column) throws SQLException {
		checkColumnIndex(column);

		String type = getColumnTypeName(column);
		if (!type.equals("SET") && !type.equals("MULTISET")
				&& !type.equals("SEQUENCE")) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.not_collection);
		}

		return ele_type[column - 1];
	}

	public String getElementTypeName(int column) throws SQLException {
		checkColumnIndex(column);

		String type = getColumnTypeName(column);
		if (!type.equals("SET") && !type.equals("MULTISET")
				&& !type.equals("SEQUENCE")) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.not_collection);
		}

		return ele_type_name[column - 1];
	}

	/* JDK 1.6 */
	public boolean isWrapperFor(Class<?> iface) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public <T> T unwrap(Class<T> iface) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	private void checkColumnIndex(int column) throws SQLException {
		if (column < 1 || column > col_name.length) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_index);
		}
	}
}
