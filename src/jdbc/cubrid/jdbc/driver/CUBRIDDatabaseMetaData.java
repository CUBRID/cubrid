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

import java.sql.Connection;
import java.sql.DatabaseMetaData;
import java.sql.ResultSet;
import java.sql.RowIdLifetime;
import java.sql.SQLException;

import java.util.StringTokenizer;

import cubrid.jdbc.jci.UColumnInfo;
import cubrid.jdbc.jci.UConnection;
import cubrid.jdbc.jci.UError;
import cubrid.jdbc.jci.UErrorCode;
import cubrid.jdbc.jci.USchType;
import cubrid.jdbc.jci.UStatement;
import cubrid.jdbc.jci.UUType;
import cubrid.jdbc.jci.UShardInfo;

/**
 * Title: CUBRID JDBC Driver Description:
 * 
 * @version 2.0
 */

public class CUBRIDDatabaseMetaData implements DatabaseMetaData {
	private static final int  SQL_MAX_CHAR_LITERAL_LEN = 1073741823;
	
	CUBRIDConnection con;
	UConnection u_con;
	UError error;
	boolean is_closed;
	int major_version;
	int minor_version;
	int shard_id = UShardInfo.SHARD_ID_INVALID;

	protected CUBRIDDatabaseMetaData(CUBRIDConnection c) {
		con = c;
		u_con = con.u_con;
		error = null;
		is_closed = false;
		major_version = -1;
		minor_version = -1;
		shard_id = 0;		// default SHARD #0
	}

	protected CUBRIDDatabaseMetaData(CUBRIDConnection c, int sid) {
		con = c;
		u_con = con.u_con;
		error = null;
		is_closed = false;
		major_version = -1;
		minor_version = -1;
		shard_id = sid;
	}

	/*
	 * java.sql.DatabaseMetaData interface
	 */

	public synchronized boolean allProceduresAreCallable() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean allTablesAreSelectable() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized String getURL() throws SQLException {
		checkIsOpen();
		return con.url;
	}

	public synchronized String getUserName() throws SQLException {
		checkIsOpen();
		return con.user;
	}

	public synchronized boolean isReadOnly() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean nullsAreSortedHigh() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean nullsAreSortedLow() throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean nullsAreSortedAtStart() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean nullsAreSortedAtEnd() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized String getDatabaseProductName() throws SQLException {
		checkIsOpen();
		return "CUBRID";
	}

	public synchronized String getDatabaseProductVersion() throws SQLException {
		checkIsOpen();

		String ver = null;
		synchronized (u_con) {
			ver = u_con.getDatabaseProductVersion();
			error = u_con.getRecentError();
		}

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			StringTokenizer st = new StringTokenizer(ver, ".");
			if (st.countTokens() == 4) {	// ex) 8.4.9.9999(major.minor.patch.build
				this.major_version = Integer.parseInt(st.nextToken());
				this.minor_version = Integer.parseInt(st.nextToken());
			}
			
			return ver;
		default:
			throw con.createCUBRIDException(error);
		}
	}

	public synchronized String getDriverName() throws SQLException {
		checkIsOpen();
		
		return "CUBRID JDBC Driver";
	}

	public synchronized String getDriverVersion() throws SQLException {
		checkIsOpen();
		
		return CUBRIDDriver.version_string;
	}

	public int getDriverMajorVersion() {
		return CUBRIDDriver.major_version;
	}

	public int getDriverMinorVersion() {
		return CUBRIDDriver.minor_version;
	}

	public synchronized boolean usesLocalFiles() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean usesLocalFilePerTable() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsMixedCaseIdentifiers()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean storesUpperCaseIdentifiers()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean storesLowerCaseIdentifiers()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean storesMixedCaseIdentifiers()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsMixedCaseQuotedIdentifiers()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean storesUpperCaseQuotedIdentifiers()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean storesLowerCaseQuotedIdentifiers()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean storesMixedCaseQuotedIdentifiers()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized String getIdentifierQuoteString() throws SQLException {
		checkIsOpen();
		return "\"";
	}

	public synchronized String getSQLKeywords() throws SQLException {
		checkIsOpen();
		return "ADD, ADD_MONTHS, AFTER, ALIAS, ASYNC, ATTACH, ATTRIBUTE, BEFORE, "
				+ "BOOLEAN, BREADTH, CALL, CHANGE, CLASS, CLASSES, CLUSTER, COMPLETION, "
				+ "CYCLE, DATA, DATA_TYPE___, DEPTH, DICTIONARY, DIFFERENCE, EACH, ELSEIF, "
				+ "EQUALS, EVALUATE, EXCLUDE, FILE, FUNCTION, GENERAL, IF, IGNORE, INCREMENT, "
				+ "INDEX, INHERIT, INOUT, INTERSECTION, LAST_DAY, LDB, LEAVE, LESS, LIMIT, "
				+ "LIST, LOOP, LPAD, LTRIM, MAXVALUE, METHOD, MINVALUE, MODIFY, MONETARY, "
				+ "MONTHS_BETWEEN, MULTISET, MULTISET_OF, NA, NOCYCLE, NOMAXVALUE, NOMINVALUE, "
				+ "NONE, OBJECT, OFF, OID, OLD, OPERATION, OPERATORS, OPTIMIZATION, OTHERS, "
				+ "OUT, PARAMETERS, PENDANT, PREORDER, PRIVATE, PROXY, PROTECTED, QUERY, "
				+ "RECURSIVE, REF, REFERENCING, REGISTER, RENAME, REPLACE, RESIGNAL, RETURN, "
				+ "RETURNS, ROLE, ROUTINE, ROW, RPAD, RTRIM, SAVEPOINT, SCOPE___, SEARCH, "
				+ "SENSITIVE, SEQUENCE, SEQUENCE_OF, SERIAL, SERIALIZABLE, SETEQ, SETNEQ, "
				+ "SET_OF, SHARED, SHORT, SIGNAL, SIMILAR, SQLEXCEPTION, SQLWARNING, START, "
				+ "TATISTICS, STDDEV, STRING, STRUCTURE, SUBCLASS, SUBSET, SUBSETEQ, "
				+ "SUPERCLASS, SUPERSET, SUPERSETEQ, SYS_DATE, SYS_TIME, SYS_TIMESTAMP, "
				+ "SYS_USER, TEST, THERE, TO_CHAR, TO_DATE, TO_NUMBER, TO_TIME, TO_TIMESTAMP, "
				+ "TRIGGER, TYPE, UNDER, USE, UTIME, VARIABLE, VARIANCE, VCLASS, VIRTUAL, "
				+ "VISIBLE, WAIT, WHILE, WITHOUT, SYS_DATETIME, TO_DATETIME";
	}

	public synchronized String getNumericFunctions() throws SQLException {
		checkIsOpen();
		return "AVG, COUNT, MAX, MIN, STDDEV, SUM, VARIANCE";
	}

	public synchronized String getStringFunctions() throws SQLException {
		checkIsOpen();
		return "BIT_LENGTH, CHAR_LENGTH, LOWER, LTRIM, OCTET_LENGTH, POSITION, REPLACE, "
				+ "RPAD, RTRIM, SUBSTRING, TRANSLATE, TRIM, TO_CHAR, TO_DATE, TO_NUMBER, "
				+ "TO_TIME, TO_TIMESTAMP, TO_DATETIME, UPPER";
	}

	public synchronized String getSystemFunctions() throws SQLException {
		checkIsOpen();
		return "";
	}

	public synchronized String getTimeDateFunctions() throws SQLException {
		checkIsOpen();
		return "ADD_MONTHS, LAST_DAY, MONTH_BETWEEN, SYS_DATE, SYS_TIME, SYS_TIMESTMAP, TO_DATE, TO_TIME, TO_TIMESTAMP, TO_DATETIME";
	}

	public synchronized String getSearchStringEscape() throws SQLException {
		checkIsOpen();
		return null;
	}

	public synchronized String getExtraNameCharacters() throws SQLException {
		checkIsOpen();
		return "%#";
	}

	public synchronized boolean supportsAlterTableWithAddColumn()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsAlterTableWithDropColumn()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsColumnAliasing() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean nullPlusNonNullIsNull() throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsConvert() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsConvert(int fromType, int toType)
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsTableCorrelationNames()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsDifferentTableCorrelationNames()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsExpressionsInOrderBy()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsOrderByUnrelated() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsGroupBy() throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsGroupByUnrelated() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsGroupByBeyondSelect()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsLikeEscapeClause() throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsMultipleResultSets()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsMultipleTransactions()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsNonNullableColumns()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsMinimumSQLGrammar() throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsCoreSQLGrammar() throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsExtendedSQLGrammar()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsANSI92EntryLevelSQL()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsANSI92IntermediateSQL()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsANSI92FullSQL() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsIntegrityEnhancementFacility()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsOuterJoins() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsFullOuterJoins() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsLimitedOuterJoins() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized String getSchemaTerm() throws SQLException {
		checkIsOpen();
		return "";
	}

	public synchronized String getProcedureTerm() throws SQLException {
		checkIsOpen();
		return "";
	}

	public synchronized String getCatalogTerm() throws SQLException {
		checkIsOpen();
		return null;
	}

	public synchronized boolean isCatalogAtStart() throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized String getCatalogSeparator() throws SQLException {
		checkIsOpen();
		return null;
	}

	public synchronized boolean supportsSchemasInDataManipulation()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsSchemasInProcedureCalls()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsSchemasInTableDefinitions()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsSchemasInIndexDefinitions()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsSchemasInPrivilegeDefinitions()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsCatalogsInDataManipulation()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsCatalogsInProcedureCalls()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsCatalogsInTableDefinitions()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsCatalogsInIndexDefinitions()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsCatalogsInPrivilegeDefinitions()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsPositionedDelete() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsPositionedUpdate() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsSelectForUpdate() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsStoredProcedures() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsSubqueriesInComparisons()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsSubqueriesInExists()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsSubqueriesInIns() throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsSubqueriesInQuantifieds()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsCorrelatedSubqueries()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsUnion() throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsUnionAll() throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsOpenCursorsAcrossCommit()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsOpenCursorsAcrossRollback()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsOpenStatementsAcrossCommit()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsOpenStatementsAcrossRollback()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized int getMaxBinaryLiteralLength() throws SQLException {
		checkIsOpen();
		return (SQL_MAX_CHAR_LITERAL_LEN / 8);
	}

	public synchronized int getMaxCharLiteralLength() throws SQLException {
		checkIsOpen();
		return SQL_MAX_CHAR_LITERAL_LEN;
	}

	public synchronized int getMaxColumnNameLength() throws SQLException {
		checkIsOpen();
		return 254;
	}

	public synchronized int getMaxColumnsInGroupBy() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxColumnsInIndex() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxColumnsInOrderBy() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxColumnsInSelect() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxColumnsInTable() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxConnections() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxCursorNameLength() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxIndexLength() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxSchemaNameLength() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxProcedureNameLength() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxCatalogNameLength() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxRowSize() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized boolean doesMaxRowSizeIncludeBlobs()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized int getMaxStatementLength() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxStatements() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxTableNameLength() throws SQLException {
		checkIsOpen();
		return 254;
	}

	public synchronized int getMaxTablesInSelect() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getMaxUserNameLength() throws SQLException {
		checkIsOpen();
		return 31;
	}

	public synchronized int getDefaultTransactionIsolation()
			throws SQLException {
		checkIsOpen();
		return Connection.TRANSACTION_READ_COMMITTED;
	}

	public synchronized boolean supportsTransactions() throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsTransactionIsolationLevel(int level)
			throws SQLException {
		checkIsOpen();
		switch (level) {
		case Connection.TRANSACTION_READ_COMMITTED:
		case Connection.TRANSACTION_READ_UNCOMMITTED:
		case Connection.TRANSACTION_REPEATABLE_READ:
		case Connection.TRANSACTION_SERIALIZABLE:
		case CUBRIDConnection.TRAN_REP_CLASS_COMMIT_INSTANCE:
		case CUBRIDConnection.TRAN_REP_CLASS_UNCOMMIT_INSTANCE:
			return true;
		default:
			return false;
		}
	}

	public synchronized boolean supportsDataDefinitionAndDataManipulationTransactions()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsDataManipulationTransactionsOnly()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean dataDefinitionCausesTransactionCommit()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean dataDefinitionIgnoredInTransactions()
			throws SQLException {
		checkIsOpen();
		return false;
	}

	/*
	 * empty ResultSet
	 */
	public synchronized ResultSet getProcedures(String catalog,
			String schemaPattern, String procedureNamePattern)
			throws SQLException {
		checkIsOpen();

		String[] names = { "PROCEDURE_CAT", "PROCEDURE_SCHEM",
				"PROCEDURE_NAME", "", "", "", "REMARKS", "PROCEDURE_TYPE" };
		int[] types = { UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_SHORT };
		boolean[] nullable = { true, true, false, true, true, true, false,
				false };

		return new CUBRIDResultSetWithoutQuery(8, types, names, nullable, null);
	}

	/*
	 * empty ResultSet
	 */
	public synchronized ResultSet getProcedureColumns(String catalog,
			String schemaPattern, String procedureNamePattern,
			String columnNamePattern) throws SQLException {
		checkIsOpen();

		String[] names = { "PROCEDURE_CAT", "PROCEDURE_SCHEM",
				"PROCEDURE_NAME", "COLUMN_NAME", "COLUMN_TYPE", "DATA_TYPE",
				"TYPE_NAME", "PRECISION", "LENGTH", "SCALE", "RADIX",
				"NULLABLE", "REMARKS" };
		int[] types = { UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_SHORT, UUType.U_TYPE_SHORT,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_INT, UUType.U_TYPE_INT,
				UUType.U_TYPE_SHORT, UUType.U_TYPE_SHORT, UUType.U_TYPE_SHORT,
				UUType.U_TYPE_VARCHAR };
		boolean[] nullable = { true, true, false, false, false, false, false,
				false, false, false, false, false, false };

		return new CUBRIDResultSetWithoutQuery(13, types, names, nullable, null);
	}

	public synchronized ResultSet getTables(String catalog,
			String schemaPattern, String tableNamePattern, String[] types)
			throws SQLException {
		checkIsOpen();

		String[] names = { "TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME",
				"TABLE_TYPE", "REMARKS" };
		int[] type = { UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR };
		boolean[] nullable = { true, true, false, false, false };

		UStatement us = null;
		synchronized (u_con) {
			us = u_con.getSchemaInfo(USchType.SCH_CLASS, tableNamePattern,
					null, (byte) 3, shard_id);
			error = u_con.getRecentError();
			switch (error.getErrorCode()) {
			case UErrorCode.ER_NO_ERROR:
				break;
			case UErrorCode.ER_IS_CLOSED:
				close();
				throw new CUBRIDException(CUBRIDJDBCErrorCode.dbmetadata_closed);
			default:
				throw con.createCUBRIDException(error);
			}
		}

		UColumnInfo[] column_info = us.getColumnInfo();
		int[] precision = new int[5];
		precision[0] = 0; /* TABLE_CAT */
		precision[1] = 0; /* TABLE_SCHEM */
		precision[2] = column_info[0].getColumnPrecision(); /* TABLE_NAME */
		precision[3] = 12; /* TABLE_TYPE */
		precision[4] = 0; /* REMARKS */
		CUBRIDResultSetWithoutQuery rs = new CUBRIDResultSetWithoutQuery(5,
				type, names, nullable, precision);

		Object[] value = new Object[5];
		value[0] = null;
		value[1] = null;
		value[4] = null;

		// TABLE type
		int j = 0;
		if (types != null) {
			for (j = 0; j < types.length; j++)
				if (types[j].equalsIgnoreCase("TABLE"))
					break;
		}

		if (types == null || j < types.length) {
			value[3] = "TABLE";

			int i = 0;
			while (true) {
				us.moveCursor(i++, UStatement.CURSOR_SET);
				if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR)
					break;
				us.fetch();

				if (us.getInt(1) != 2)
					continue;

				value[2] = us.getString(0);
				rs.addTuple(value);
			}
		}

		// VIEW type
		if (types != null) {
			for (j = 0; j < types.length; j++) {
				if (types[j].equalsIgnoreCase("VIEW"))
					break;
			}
		}

		if (types == null || j < types.length) {
			value[3] = "VIEW";

			int i = 0;
			while (true) {
				us.moveCursor(i++, UStatement.CURSOR_SET);
				if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR)
					break;
				us.fetch();

				if (us.getInt(1) != 1)
					continue;

				value[2] = us.getString(0);
				rs.addTuple(value);
			}
		}

		// SYSTEM TABLE type
		if (types != null) {
			for (j = 0; j < types.length; j++)
				if (types[j].equalsIgnoreCase("SYSTEM TABLE"))
					break;
		}

		if (types == null || j < types.length) {
			value[3] = "SYSTEM TABLE";

			int i = 0;
			while (true) {
				us.moveCursor(i++, UStatement.CURSOR_SET);
				if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR)
					break;
				us.fetch();

				if (us.getInt(1) != 0)
					continue;

				value[2] = us.getString(0);
				rs.addTuple(value);
			}
		}
		us.close();
		endTransaction();

		rs.sortTuples(new CUBRIDComparator("getTables"));
		return rs;
	}

	/*
	 * empty ResultSet
	 */
	public synchronized ResultSet getSchemas() throws SQLException {
		checkIsOpen();

		String[] names = { "TABLE_SCHEM" };
		int[] types = { UUType.U_TYPE_VARCHAR };
		boolean[] nullable = { false };

		return new CUBRIDResultSetWithoutQuery(1, types, names, nullable, null);
	}

	/*
	 * empty ResultSet
	 */
	public synchronized ResultSet getCatalogs() throws SQLException {
		checkIsOpen();

		String[] names = { "TABLE_CAT" };
		int[] types = { UUType.U_TYPE_VARCHAR };
		boolean[] nullable = { false };

		return new CUBRIDResultSetWithoutQuery(1, types, names, nullable, null);
	}

	public synchronized ResultSet getTableTypes() throws SQLException {
		checkIsOpen();

		String[] names = { "TABLE_TYPE" };
		int[] types = { UUType.U_TYPE_VARCHAR };
		boolean[] nullable = { false };

		CUBRIDResultSetWithoutQuery rs = new CUBRIDResultSetWithoutQuery(1,
				types, names, nullable, null);

		Object[] value = new Object[1];
		value[0] = "SYSTEM TABLE";
		rs.addTuple(value);
		value[0] = "TABLE";
		rs.addTuple(value);
		value[0] = "VIEW";
		rs.addTuple(value);

		return rs;
	}

	public synchronized ResultSet getColumns(String catalog,
			String schemaPattern, String tableNamePattern,
			String columnNamePattern) throws SQLException {
		checkIsOpen();

		String[] names = { "TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME",
				"COLUMN_NAME", "DATA_TYPE", "TYPE_NAME", "COLUMN_SIZE",
				"BUFFER_LENGTH", "DECIMAL_DIGITS", "NUM_PREC_RADIX",
				"NULLABLE", "REMARKS", "COLUMN_DEF", "SQL_DATA_TYPE",
				"SQL_DATETIME_SUB", "CHAR_OCTET_LENGTH", "ORDINAL_POSITION",
				"IS_NULLABLE" };
		int[] types = { UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_SHORT, UUType.U_TYPE_VARCHAR, UUType.U_TYPE_INT,
				UUType.U_TYPE_NULL, UUType.U_TYPE_INT, UUType.U_TYPE_INT,
				UUType.U_TYPE_INT, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_INT, UUType.U_TYPE_INT,
				UUType.U_TYPE_INT, UUType.U_TYPE_INT, UUType.U_TYPE_VARCHAR };
		boolean[] nullable = { true, true, false, false, false, false, false,
				true, false, false, false, false, true, true, true, false,
				false, false };

		CUBRIDResultSetWithoutQuery rs = new CUBRIDResultSetWithoutQuery(18,
				types, names, nullable, null);

		UStatement us = null;
		synchronized (u_con) {
			int flag = 0;
			if (tableNamePattern == null || containsWildcard(tableNamePattern))
				flag |= 1;
			if (columnNamePattern == null || containsWildcard(columnNamePattern))
				flag |= 2;
			us = u_con.getSchemaInfo(USchType.SCH_ATTRIBUTE, tableNamePattern,
					columnNamePattern, (byte) flag, shard_id);
			error = u_con.getRecentError();
			switch (error.getErrorCode()) {
			case UErrorCode.ER_NO_ERROR:
				break;
			case UErrorCode.ER_IS_CLOSED:
				close();
				throw new CUBRIDException(CUBRIDJDBCErrorCode.dbmetadata_closed);
			default:
				throw con.createCUBRIDException(error);
			}
		}

		Object[] value = new Object[18];

		value[0] = null;
		value[1] = null;
		value[7] = null;
		value[9] = new Integer(10);
		value[11] = null;
		value[13] = null;
		value[14] = null;

		int i = 0;
		while (true) {
			us.moveCursor(i++, UStatement.CURSOR_SET);
			if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR)
				break;
			us.fetch();
			int err_code = us.getRecentError().getErrorCode();
			if (err_code != UErrorCode.ER_NO_ERROR
					&& err_code != UErrorCode.ER_WAS_NULL) {
				throw con.createCUBRIDException(us.getRecentError());
			}

			// type-independent decisions
			value[2] = us.getString(10);
			value[3] = us.getString(0);
			value[6] = value[15] = new Integer(us.getInt(3));
			value[8] = new Integer(us.getInt(2));
			value[16] = new Integer(us.getInt(9));
			if (us.getInt(5) == 1) {
				value[10] = new Integer(columnNoNulls);
				value[17] = "NO";
			} else {
				value[10] = new Integer(columnNullable);
				value[17] = "YES";
			}
			value[12] = us.getObject(8);

			// type-dependent decisions
			int type = us.getInt(1);
			if (type == UUType.U_TYPE_BIT) {
				value[4] = new Short((short) java.sql.Types.BINARY);
				value[5] = "BIT";
			} else if (type == UUType.U_TYPE_VARBIT) {
				value[4] = new Short((short) java.sql.Types.VARBINARY);
				value[5] = "BIT VARYING";
			} else if (type == UUType.U_TYPE_CHAR) {
				value[4] = new Short((short) java.sql.Types.CHAR);
				value[5] = "CHAR";
			} else if (type == UUType.U_TYPE_VARCHAR) {
				value[4] = new Short((short) java.sql.Types.VARCHAR);
				value[5] = "VARCHAR";
			} else if (type == UUType.U_TYPE_ENUM) {
				value[4] = new Short((short) java.sql.Types.VARCHAR);
				value[5] = "ENUM";
			} else if (type == UUType.U_TYPE_NCHAR) {
				value[4] = new Short((short) java.sql.Types.CHAR);
				value[5] = "NCHAR";
			} else if (type == UUType.U_TYPE_VARNCHAR) {
				value[4] = new Short((short) java.sql.Types.VARCHAR);
				value[5] = "NCHAR VARYING";
			} else if (type == UUType.U_TYPE_SHORT) {
				value[4] = new Short((short) java.sql.Types.SMALLINT);
				value[5] = "SMALLINT";
			} else if (type == UUType.U_TYPE_BIGINT) {
				value[4] = new Short((short) java.sql.Types.BIGINT);
				value[5] = "BIGINT";
			} else if (type == UUType.U_TYPE_INT) {
				value[4] = new Short((short) java.sql.Types.INTEGER);
				value[5] = "INTEGER";
			} else if (type == UUType.U_TYPE_NUMERIC) {
				value[4] = new Short((short) java.sql.Types.NUMERIC);
				value[5] = "NUMERIC";
			} else if (type == UUType.U_TYPE_FLOAT) {
				value[4] = new Short((short) java.sql.Types.REAL);
				value[5] = "FLOAT";
			} else if (type == UUType.U_TYPE_DOUBLE) {
				value[4] = new Short((short) java.sql.Types.DOUBLE);
				value[5] = "DOUBLE PRECISION";
			} else if (type == UUType.U_TYPE_MONETARY) {
				value[4] = new Short((short) java.sql.Types.DOUBLE);
				value[5] = "MONETARY";
			} else if (type == UUType.U_TYPE_TIME) {
				value[4] = new Short((short) java.sql.Types.TIME);
				value[5] = "TIME";
			} else if (type == UUType.U_TYPE_DATE) {
				value[4] = new Short((short) java.sql.Types.DATE);
				value[5] = "DATE";
			} else if (type == UUType.U_TYPE_TIMESTAMP) {
				value[4] = new Short((short) java.sql.Types.TIMESTAMP);
				value[5] = "TIMESTAMP";
			} else if (type == UUType.U_TYPE_DATETIME) {
				value[4] = new Short((short) java.sql.Types.TIMESTAMP);
				value[5] = "DATETIME";
			} else if (type == UUType.U_TYPE_OBJECT) {
				value[4] = new Short((short) java.sql.Types.OTHER);
				value[5] = "CLASS";
			} else if (type == UUType.U_TYPE_SET) {
				value[4] = new Short((short) java.sql.Types.OTHER);
				value[5] = "SET";
			} else if (type == UUType.U_TYPE_MULTISET) {
				value[4] = new Short((short) java.sql.Types.OTHER);
				value[5] = "MULTISET";
			} else if (type == UUType.U_TYPE_SEQUENCE) {
				value[4] = new Short((short) java.sql.Types.OTHER);
				value[5] = "SEQUENCE";
			} else if (type == UUType.U_TYPE_BLOB) {
				value[4] = new Short((short) java.sql.Types.BLOB);
				value[5] = "BLOB";
			} else if (type == UUType.U_TYPE_CLOB) {
				value[4] = new Short((short) java.sql.Types.CLOB);
				value[5] = "CLOB";
			}

			rs.addTuple(value);
		}
		us.close();
		endTransaction();

		rs.sortTuples(new CUBRIDComparator("getColumns"));
		return rs;
	}

	public synchronized ResultSet getColumnPrivileges(String catalog,
			String schema, String table, String columnNamePattern)
			throws SQLException {
		checkIsOpen();

		String[] names = { "TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME",
				"COLUMN_NAME", "GRANTOR", "GRANTEE", "PRIVILEGE",
				"IS_GRANTABLE" };
		int[] types = { UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR };
		boolean[] nullable = { true, true, false, false, true, false, false,
				false };

		CUBRIDResultSetWithoutQuery rs = new CUBRIDResultSetWithoutQuery(8,
				types, names, nullable, null);

		UStatement us = null;
		synchronized (u_con) {
			us = u_con.getSchemaInfo(USchType.SCH_ATTR_PRIVILEGE, table,
					columnNamePattern, (byte) 2, shard_id);
			error = u_con.getRecentError();
			switch (error.getErrorCode()) {
			case UErrorCode.ER_NO_ERROR:
				break;
			case UErrorCode.ER_IS_CLOSED:
				throw con.createCUBRIDException(CUBRIDJDBCErrorCode.dbmetadata_closed, null);
			default:
				throw con.createCUBRIDException(error);
			}
		}

		Object[] value = new Object[8];

		value[0] = null;
		value[1] = null;
		value[2] = table;

		int i = 0;
		while (true) {
			us.moveCursor(i++, UStatement.CURSOR_SET);
			if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR)
				break;
			us.fetch();

			value[3] = us.getString(0);
			value[4] = null;
			value[5] = con.user;
			value[6] = us.getString(1);
			value[7] = us.getString(2);

			rs.addTuple(value);
		}
		us.close();
		endTransaction();

		rs.sortTuples(new CUBRIDComparator("getColumnPrivileges"));
		return rs;
	}

	public synchronized ResultSet getTablePrivileges(String catalog,
			String schemaPattern, String tableNamePattern) throws SQLException {
		checkIsOpen();

		String[] names = { "TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "GRANTOR",
				"GRANTEE", "PRIVILEGE", "IS_GRANTABLE" };
		int[] types = { UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR };
		boolean[] nullable = { true, true, false, true, false, false, false };

		CUBRIDResultSetWithoutQuery rs = new CUBRIDResultSetWithoutQuery(7,
				types, names, nullable, null);

		UStatement us = null;
		synchronized (u_con) {
			us = u_con.getSchemaInfo(USchType.SCH_CLASS_PRIVILEGE,
					tableNamePattern, null, (byte) 3, shard_id);
			error = u_con.getRecentError();
			switch (error.getErrorCode()) {
			case UErrorCode.ER_NO_ERROR:
				break;
			case UErrorCode.ER_IS_CLOSED:
				close();
				throw new CUBRIDException(CUBRIDJDBCErrorCode.dbmetadata_closed);
			default:
				throw con.createCUBRIDException(error);
			}
		}

		Object[] value = new Object[7];

		value[0] = null;
		value[1] = null;

		int i = 0;
		while (true) {
			us.moveCursor(i++, UStatement.CURSOR_SET);
			if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR)
				break;
			us.fetch();

			value[2] = us.getString(0);
			value[3] = null;
			value[4] = con.user;
			value[5] = us.getString(1);
			value[6] = us.getString(2);

			rs.addTuple(value);
		}
		us.close();
		endTransaction();

		rs.sortTuples(new CUBRIDComparator("getTablePrivileges"));
		return rs;
	}

	public synchronized ResultSet getBestRowIdentifier(String catalog,
			String schema, String table, int scope, boolean nullable)
			throws SQLException {
		checkIsOpen();

		String[] names = { "SCOPE", "COLUMN_NAME", "DATA_TYPE", "TYPE_NAME",
				"COLUMN_SIZE", "BUFFER_LENGTH", "DECIMAL_DIGITS",
				"PSEUDO_COLUMN" };
		int[] types = { UUType.U_TYPE_SHORT, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_SHORT, UUType.U_TYPE_VARCHAR, UUType.U_TYPE_INT,
				UUType.U_TYPE_INT, UUType.U_TYPE_SHORT, UUType.U_TYPE_SHORT };
		boolean[] Nullable = { false, false, false, false, false, true, false,
				false };

		CUBRIDResultSetWithoutQuery rs = new CUBRIDResultSetWithoutQuery(8,
				types, names, Nullable, null);

		UStatement us = null;
		synchronized (u_con) {
			us = u_con.getSchemaInfo(USchType.SCH_CONSTRAIT, table, null,
					(byte) 2, shard_id);
			error = u_con.getRecentError();
			switch (error.getErrorCode()) {
			case UErrorCode.ER_NO_ERROR:
				break;
			case UErrorCode.ER_IS_CLOSED:
				close();
				throw new CUBRIDException(CUBRIDJDBCErrorCode.dbmetadata_closed);
			default:
				throw con.createCUBRIDException(error);
			}
		}

		int i = 0, min = 2100000000, minindex = -1;
		while (true) {
			us.moveCursor(i++, UStatement.CURSOR_SET);
			if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR)
				break;
			us.fetch();

			if (us.getInt(0) != 0)
				continue;

			String const_name = us.getString(1);
			int num_of_comma = 0, last_occur = -1;

			while ((last_occur = const_name.indexOf(',', last_occur + 1)) != -1)
				num_of_comma++;

			if (num_of_comma < min) {
				min = num_of_comma;
				minindex = i;
			}
		}

		if (min == 2100000000) {
			us.close();
			endTransaction();

			return rs;
		}

		UStatement us2 = null;
		synchronized (u_con) {
			us2 = u_con.getSchemaInfo(USchType.SCH_ATTRIBUTE, table, null,
					(byte) 2, shard_id);
			error = u_con.getRecentError();
			switch (error.getErrorCode()) {
			case UErrorCode.ER_NO_ERROR:
				break;
			case UErrorCode.ER_IS_CLOSED:
				close();
				throw con.createCUBRIDException(CUBRIDJDBCErrorCode.dbmetadata_closed, null);
			default:
				throw con.createCUBRIDException(error);
			}
		}

		Object[] value = new Object[8];
		value[5] = null;
		value[7] = new Short((short) DatabaseMetaData.bestRowNotPseudo);

		for (i = 0; i <= min; i++) {
			us.moveCursor(minindex + i, UStatement.CURSOR_SET);
			us.fetch();

			value[1] = us.getString(2);

			int j = 0;
			while (true) {
				us2.moveCursor(j, UStatement.CURSOR_SET);
				us2.fetch();
				if (us2.getString(0).equals(value[1]))
					break;
				j++;
			}

			value[6] = new Integer(us2.getInt(2));

			switch (us2.getInt(1)) {
			case UUType.U_TYPE_CHAR:
				value[2] = new Integer(java.sql.Types.CHAR);
				value[3] = "CHAR";
				value[4] = new Integer(0);
				break;
			case UUType.U_TYPE_VARCHAR:
				value[2] = new Integer(java.sql.Types.VARCHAR);
				value[3] = "VARCHAR";
				value[4] = new Integer(0);
				break;
			case UUType.U_TYPE_ENUM:
				value[2] = new Integer(java.sql.Types.VARCHAR);
				value[3] = "ENUM";
				value[4] = new Integer(0);
				break;
			case UUType.U_TYPE_SHORT:
				value[2] = new Integer(java.sql.Types.SMALLINT);
				value[3] = "SMALLINT";
				value[4] = new Integer(us2.getInt(3));
				break;
			case UUType.U_TYPE_INT:
				value[2] = new Integer(java.sql.Types.INTEGER);
				value[3] = "INTEGER";
				value[4] = new Integer(us2.getInt(3));
				break;
			case UUType.U_TYPE_BIGINT:
				value[2] = new Integer(java.sql.Types.BIGINT);
				value[3] = "BIGINT";
				value[4] = new Integer(us2.getInt(3));
				break;
			case UUType.U_TYPE_DOUBLE:
				value[2] = new Integer(java.sql.Types.DOUBLE);
				value[3] = "DOUBLE";
				value[4] = new Integer(us2.getInt(3));
				break;
			case UUType.U_TYPE_FLOAT:
				value[2] = new Integer(java.sql.Types.REAL);
				value[3] = "FLOAT";
				value[4] = new Integer(us2.getInt(3));
				break;
			case UUType.U_TYPE_NUMERIC:
				value[2] = new Integer(java.sql.Types.NUMERIC);
				value[3] = "NUMERIC";
				value[4] = new Integer(us2.getInt(3));
				break;
			case UUType.U_TYPE_DATE:
				value[2] = new Integer(java.sql.Types.DATE);
				value[3] = "DATE";
				value[4] = new Integer(0);
				break;
			case UUType.U_TYPE_TIME:
				value[2] = new Integer(java.sql.Types.TIME);
				value[3] = "TIME";
				value[4] = new Integer(0);
				break;
			case UUType.U_TYPE_TIMESTAMP:
				value[2] = new Integer(java.sql.Types.TIMESTAMP);
				value[3] = "TIMESTAMP";
				value[4] = new Integer(0);
				break;
			case UUType.U_TYPE_DATETIME:
				value[2] = new Integer(java.sql.Types.TIMESTAMP);
				value[3] = "DATETIME";
				value[4] = new Integer(0);
				break;
			case UUType.U_TYPE_NULL:
				value[2] = new Integer(java.sql.Types.NULL);
				value[3] = "";
				value[4] = new Integer(0);
				break;
			case UUType.U_TYPE_BLOB:
				value[2] = new Integer(java.sql.Types.BLOB);
				value[3] = "BLOB";
				value[4] = new Integer(0);
				break;
			case UUType.U_TYPE_CLOB:
				value[2] = new Integer(java.sql.Types.CLOB);
				value[3] = "CLOB";
				value[4] = new Integer(0);
				break;
			}

			rs.addTuple(value);
		}
		us.close();
		us2.close();
		endTransaction();

		rs.sortTuples(new CUBRIDComparator("getBestRowIdentifier"));
		return rs;
	}

	/*
	 * empty ResultSet
	 */
	public synchronized ResultSet getVersionColumns(String catalog,
			String schema, String table) throws SQLException {
		checkIsOpen();

		String[] names = { "SCOPE", "COLUMN_NAME", "DATA_TYPE", "TYPE_NAME",
				"COLUMN_SIZE", "BUFFER_LENGTH", "DECIMAL_DIGITS",
				"PSEUDO_COLUMN" };
		int[] types = { UUType.U_TYPE_SHORT, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_SHORT, UUType.U_TYPE_VARCHAR, UUType.U_TYPE_INT,
				UUType.U_TYPE_INT, UUType.U_TYPE_SHORT, UUType.U_TYPE_SHORT };
		boolean[] nullable = { true, false, false, false, false, false, false,
				false };

		return new CUBRIDResultSetWithoutQuery(8, types, names, nullable, null);
	}

	public synchronized ResultSet getPrimaryKeys(String catalog, String schema,
			String table) throws SQLException {
		checkIsOpen();

		String[] names = { "TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME",
				"COLUMN_NAME", "KEY_SEQ", "PK_NAME" };

		int[] type = { UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_SHORT, UUType.U_TYPE_VARCHAR };

		boolean[] nullable = { true, true, false, false, false, false };

		UStatement us = null;
		synchronized (u_con) {
			us = u_con.getSchemaInfo(USchType.SCH_PRIMARY_KEY, table, null,
					(byte) 3, shard_id);
			error = u_con.getRecentError();
			switch (error.getErrorCode()) {
			case UErrorCode.ER_NO_ERROR:
				break;
			case UErrorCode.ER_IS_CLOSED:
				close();
				throw new CUBRIDException(CUBRIDJDBCErrorCode.dbmetadata_closed);
			default:
				throw con.createCUBRIDException(error);
			}
		}

		CUBRIDResultSetWithoutQuery rs = new CUBRIDResultSetWithoutQuery(6,
				type, names, nullable, null);
		Object[] value = new Object[6];

		value[0] = null;
		value[1] = null;

		int i = 0;
		while (true) {
			us.moveCursor(i++, UStatement.CURSOR_SET);
			if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR)
				break;
			us.fetch();

			value[2] = us.getString(0);
			value[3] = us.getString(1);
			value[4] = us.getInt(2);
			value[5] = us.getString(3);

			rs.addTuple(value);
		}

		us.close();
		endTransaction();

		return rs;
	}

	private short convertForeignKeyAction(short serverAction) {
		/*
		 * Refer to object/class_object.h
		 * 
		 * typedef enum { SM_FOREIGN_KEY_CASCADE, SM_FOREIGN_KEY_RESTRICT,
		 * SM_FOREIGN_KEY_NO_ACTION, SM_FOREIGN_KEY_SET_NULL }
		 * SM_FOREIGN_KEY_ACTION;
		 */
		switch (serverAction) {
		case 0:
			return DatabaseMetaData.importedKeyCascade;
		case 1:
			return DatabaseMetaData.importedKeyRestrict;
		case 2:
			return DatabaseMetaData.importedKeyNoAction;
		case 3:
			return DatabaseMetaData.importedKeySetNull;
		default:
			return -1;
		}
	}

	private synchronized ResultSet getForeignKeys(int type, String table1,
			String table2) throws SQLException {
		checkIsOpen();

		String[] names = { "PKTABLE_CAT", "PKTABLE_SCHEM", "PKTABLE_NAME",
				"PKCOLUMN_NAME", "FKTABLE_CAT", "FKTABLE_SCHEM",
				"FKTABLE_NAME", "FKCOLUMN_NAME", "KEY_SEQ", "UPDATE_RULE",
				"DELETE_RULE", "FK_NAME", "PK_NAME", "DEFERRABILITY" };
		int[] types = { UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_SHORT, UUType.U_TYPE_SHORT, UUType.U_TYPE_SHORT,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_SHORT };
		boolean[] nullable = { true, true, false, false, true, true, false,
				false, false, false, false, true, true, false };

		UStatement us = null;
		synchronized (u_con) {
			us = u_con.getSchemaInfo(type, table1, table2, (byte) 3, shard_id);
			error = u_con.getRecentError();
			switch (error.getErrorCode()) {
			case UErrorCode.ER_NO_ERROR:
				break;
			case UErrorCode.ER_IS_CLOSED:
				close();
				throw new CUBRIDException(CUBRIDJDBCErrorCode.dbmetadata_closed);
			default:
				throw con.createCUBRIDException(error);
			}
		}

		CUBRIDResultSetWithoutQuery rs = new CUBRIDResultSetWithoutQuery(14,
				types, names, nullable, null);
		Object[] value = new Object[14];

		/* 1. PKTABLE_CAT (String) */
		value[0] = null;
		/* 2. PKTABLE_SCHEM (String) */
		value[1] = null;
		/* 5. FKTABLE_CAT (String) */
		value[4] = null;
		/* 6. FKTABLE_SCHEM (String) */
		value[5] = null;
		/* 14. DEFERRABILITY (short) */
		value[13] = DatabaseMetaData.importedKeyInitiallyImmediate;

		int i = 0;
		while (true) {
			us.moveCursor(i++, UStatement.CURSOR_SET);
			if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR) {
				break;
			}
			us.fetch();

			/* 3. PKTABLE_NAME (String) */
			value[2] = us.getString(0);

			/* 4. PKCOLUMN_NAME (String) */
			value[3] = us.getString(1);

			/* 7. FKTABLE_NAME (String) */
			value[6] = us.getString(2);

			/* 8. FKCOLUMN_NAME (String) */
			value[7] = us.getString(3);

			/* 9. KEY_SEQ (short) */
			value[8] = us.getShort(4);

			/* 10. UPDATE_RULE (short) */
			value[9] = convertForeignKeyAction(us.getShort(5));

			/* 11. DELETE_RULE (short) */
			value[10] = convertForeignKeyAction(us.getShort(6));

			/* 12. FK_NAME (String) */
			value[11] = us.getString(7);

			/* 13. PK_NAME (String) */
			value[12] = us.getString(8);

			rs.addTuple(value);
		}

		us.close();
		endTransaction();

		return rs;
	}

	public synchronized ResultSet getImportedKeys(String catalog,
			String schema, String table) throws SQLException {
		checkIsOpen();
		
		if (table == null) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_table_name, null);
		}
		return getForeignKeys(USchType.SCH_IMPORTED_KEYS, table, null);
	}

	public synchronized ResultSet getExportedKeys(String catalog,
			String schema, String table) throws SQLException {
		checkIsOpen();
		
		if (table == null) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_table_name, null);
		}
		return getForeignKeys(USchType.SCH_EXPORTED_KEYS, table, null);
	}

	public synchronized ResultSet getCrossReference(String primaryCatalog,
			String primarySchema, String primaryTable, String foreignCatalog,
			String foreignSchema, String foreignTable) throws SQLException {
		checkIsOpen();
		
		if (primaryTable == null || foreignTable == null) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_table_name, null);
		}
		return getForeignKeys(USchType.SCH_CROSS_REFERENCE, primaryTable,
				foreignTable);
	}

	public synchronized ResultSet getTypeInfo() throws SQLException {
		checkIsOpen();

		String[] names = { "TYPE_NAME", "DATA_TYPE", "PRECISION",
				"LITERAL_PREFIX", "LITERAL_SUFFIX", "CREATE_PARAMS",
				"NULLABLE", "CASE_SENSITIVE", "SEARCHABLE",
				"UNSIGNED_ATTRIBUTE", "FIXED_PREC_SCALE", "AUTO_INCREMENT",
				"LOCAL_TYPE_NAME", "MINIMUM_SCALE", "MAXIMUM_SCALE",
				"SQL_DATA_TYPE", "SQL_DATETIME_SUB", "NUM_PREC_RADIX" };
		int[] types = { UUType.U_TYPE_VARCHAR, UUType.U_TYPE_SHORT,
				UUType.U_TYPE_INT, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_SHORT, UUType.U_TYPE_BIT, UUType.U_TYPE_SHORT,
				UUType.U_TYPE_BIT, UUType.U_TYPE_BIT, UUType.U_TYPE_BIT,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_SHORT,
				UUType.U_TYPE_SHORT, UUType.U_TYPE_INT, UUType.U_TYPE_INT,
				UUType.U_TYPE_INT };
		boolean[] nullable = { false, false, false, true, true, true, false,
				false, false, false, false, false, true, false, false, true,
				true, false };

		CUBRIDResultSetWithoutQuery rs = new CUBRIDResultSetWithoutQuery(18,
				types, names, nullable, null);
		/* Type Name */
		Object[] column1 = { "BIT", "NUMERIC", "NUMERIC", "BIT VARYING",
				"BIT VARYING", "BIT", "VARCHAR", "CHAR", "NUMERIC", "INTEGER",
				"BIGINT", "SMALLINT", "DOUBLE", "FLOAT", "DOUBLE", "VARCHAR",
				"DATE", "TIME", "TIMESTAMP", "DATETIME" };
		/* Data Type */
		Object[] column2 = { new Short((short) java.sql.Types.BIT),
				new Short((short) java.sql.Types.TINYINT),
				new Short((short) java.sql.Types.BIGINT),
				new Short((short) java.sql.Types.LONGVARBINARY),
				new Short((short) java.sql.Types.VARBINARY),
				new Short((short) java.sql.Types.BINARY),
				new Short((short) java.sql.Types.LONGVARCHAR),
				new Short((short) java.sql.Types.CHAR),
				new Short((short) java.sql.Types.NUMERIC),
				new Short((short) java.sql.Types.INTEGER),
				new Short((short) java.sql.Types.BIGINT),
				new Short((short) java.sql.Types.SMALLINT),
				new Short((short) java.sql.Types.FLOAT),
				new Short((short) java.sql.Types.REAL),
				new Short((short) java.sql.Types.DOUBLE),
				new Short((short) java.sql.Types.VARCHAR),
				new Short((short) java.sql.Types.DATE),
				new Short((short) java.sql.Types.TIME),
				new Short((short) java.sql.Types.TIMESTAMP),
				new Short((short) java.sql.Types.TIMESTAMP) };
		/* Precision */
		Object[] column3 = { new Integer(8), new Integer(3), new Integer(38),
				new Integer(1073741823), new Integer(1073741823),
				new Integer(1073741823), new Integer(1073741823),
				new Integer(1073741823), new Integer(38), new Integer(10),
				new Integer(19), new Integer(5), new Integer(38),
				new Integer(38), new Integer(38), new Integer(1073741823),
				new Integer(10), new Integer(11), new Integer(22),
				new Integer(26) };
		/* Literal prefix */
		Object[] column4 = { "B'", null, null, "X'", "X'", "X'", "'", "'",
				null, null, null, null, null, null, null, "'", "DATE'",
				"TIME'", "TIMESTAMP'", "DATETIME'" };
		/* Literal Suffix */
		Object[] column5 = { "'", null, null, "'", "'", "'", "'", "'", null,
				null, null, null, null, null, null, "'", "'", "'", "'", "'" };
		/* Create Params */
		Object[] column6 = { "(8)", "(3)", null, null, null, null, null, null,
				null, null, null, null, null, null, null, null, null, null,
				null, null };
		/* Nullable */
		Object column7 = new Short((short) typeNullable);
		/* case sensitive */
		Object[] column8 = { new Boolean(false), new Boolean(false),
				new Boolean(false), new Boolean(false), new Boolean(false),
				new Boolean(false), new Boolean(true), new Boolean(true),
				new Boolean(false), new Boolean(false), new Boolean(false),
				new Boolean(false), new Boolean(false), new Boolean(false),
				new Boolean(true), new Boolean(false), new Boolean(false),
				new Boolean(false), new Boolean(false), new Boolean(false) };
		/* Searchable */
		Object[] column9 = { new Short((short) typePredBasic),
				new Short((short) typePredBasic),
				new Short((short) typePredBasic),
				new Short((short) typePredBasic),
				new Short((short) typePredBasic),
				new Short((short) typePredBasic),
				new Short((short) typePredBasic),
				new Short((short) typeSearchable),
				new Short((short) typeSearchable),
				new Short((short) typePredBasic),
				new Short((short) typePredBasic),
				new Short((short) typePredBasic),
				new Short((short) typePredBasic),
				new Short((short) typePredBasic),
				new Short((short) typePredBasic),
				new Short((short) typeSearchable),
				new Short((short) typePredBasic),
				new Short((short) typePredBasic),
				new Short((short) typePredBasic),
				new Short((short) typePredBasic) };
		/* Unsigned attribute */
		Object[] column10 = { new Boolean(true), new Boolean(false),
				new Boolean(false), new Boolean(true), new Boolean(true),
				new Boolean(true), new Boolean(true), new Boolean(true),
				new Boolean(false), new Boolean(false), new Boolean(false),
				new Boolean(false), new Boolean(false), new Boolean(false),
				new Boolean(false), new Boolean(true), new Boolean(true),
				new Boolean(true), new Boolean(true), new Boolean(true) };
		/* FIXED_PREC_SCALE */
		Object[] column11 = { new Boolean(false), new Boolean(true),
				new Boolean(true), new Boolean(false), new Boolean(false),
				new Boolean(false), new Boolean(false), new Boolean(false),
				new Boolean(true), new Boolean(false), new Boolean(false),
				new Boolean(false), new Boolean(true), new Boolean(true),
				new Boolean(true), new Boolean(false), new Boolean(false),
				new Boolean(false), new Boolean(false), new Boolean(false) };
		/* AUTO_INCREMENT */
		Object column12 = new Boolean(false);
		/* LOCAL_TYPE_NAME */
		Object[] column13 = column1;
		/* MINIMUM_SCALE */
		Object[] column14 = { new Integer(0), new Integer(0), new Integer(0),
				new Integer(0), new Integer(0), new Integer(0), new Integer(0),
				new Integer(0), new Integer(0), new Integer(0), new Integer(0),
				new Integer(0), new Integer(0), new Integer(0), new Integer(0),
				new Integer(0), new Integer(0), new Integer(0), new Integer(0),
				new Integer(0) };
		/* MAXIMUM_SCALE */
		Object[] column15 = { new Integer(0), new Integer(0), new Integer(38),
				new Integer(0), new Integer(0), new Integer(0), new Integer(0),
				new Integer(0), new Integer(38), new Integer(0),
				new Integer(0), new Integer(0), new Integer(38),
				new Integer(38), new Integer(38), new Integer(0),
				new Integer(0), new Integer(0), new Integer(0), new Integer(0) };
		/* SQL_DATA_TYPE */
		Object column16 = null;
		/* SQL_DATETIME_SUB */
		Object column17 = column16;
		/* NUM_PREC_RADIX */
		Object column18 = new Integer(10);

		Object[] value = new Object[18];
		value[6] = column7;
		value[11] = column12;
		value[15] = column16;
		value[16] = column17;
		value[17] = column18;
		for (int i = 0; i < 18; i++) {
			value[0] = column1[i];
			value[1] = column2[i];
			value[2] = column3[i];
			value[3] = column4[i];
			value[4] = column5[i];
			value[5] = column6[i];
			value[7] = column8[i];
			value[8] = column9[i];
			value[9] = column10[i];
			value[10] = column11[i];
			value[12] = column13[i];
			value[13] = column14[i];
			value[14] = column15[i];

			rs.addTuple(value);
		}

		return rs;
	}

	public synchronized ResultSet getIndexInfo(String catalog, String schema,
			String table, boolean unique, boolean approximate)
			throws SQLException {
		checkIsOpen();

		String[] names = { "TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME",
				"NON_UNIQUE", "INDEX_QUALIFIER", "INDEX_NAME", "TYPE",
				"ORDINAL_POSITION", "COLUMN_NAME", "ASC_OR_DESC",
				"CARDINALITY", "PAGES", "FILTER_CONDITION" };
		int[] types = { UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_BIT,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_SHORT, UUType.U_TYPE_SHORT,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_INT, UUType.U_TYPE_INT, UUType.U_TYPE_VARCHAR };
		boolean[] nullable = { true, true, false, false, true, false, false,
				false, false, false, false, false, true };

		CUBRIDResultSetWithoutQuery rs = new CUBRIDResultSetWithoutQuery(13,
				types, names, nullable, null);

		UStatement us = null;
		synchronized (u_con) {
			us = u_con.getSchemaInfo(USchType.SCH_CONSTRAIT, table, null,
					(byte) 2, shard_id);
			error = u_con.getRecentError();
			switch (error.getErrorCode()) {
			case UErrorCode.ER_NO_ERROR:
				break;
			case UErrorCode.ER_IS_CLOSED:
				close();
				throw new CUBRIDException(CUBRIDJDBCErrorCode.dbmetadata_closed);
			default:
				throw con.createCUBRIDException(error);
			}
		}

		us.moveCursor(0, UStatement.CURSOR_SET);
		us.fetch();

		Object[] value = new Object[13];
		value[0] = null;
		value[1] = null;
		value[2] = table;
		value[4] = null;

		// tableIndexStatistic
		value[3] = new Boolean(false);
		value[5] = null;
		value[6] = new Short(tableIndexStatistic);
		value[7] = new Short((short) 0);
		value[8] = null;
		value[9] = null;
		value[10] = new Integer(us.getInt(4));
		value[11] = new Integer(us.getInt(3));
		value[12] = null;

		rs.addTuple(value);

		// tableIndexOther
		value[6] = new Short(tableIndexOther);
		value[9] = "A";

		int i = 0, ordinal = 1;
		String previousIndex = "";
		while (true) {
			us.moveCursor(i++, UStatement.CURSOR_SET);
			if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR)
				break;
			us.fetch();

			if (unique && us.getShort(0) == 1)
				continue;
			if (us.getShort(0) == 1)
				value[3] = new Boolean(true);
			else
				value[3] = new Boolean(false);
			value[5] = us.getString(1);
			if (((String) value[5]).equals(previousIndex))
				value[7] = new Integer(ordinal++);
			else {
				value[7] = new Integer(1);
				ordinal = 2;
				previousIndex = (String) value[5];
			}
			value[8] = us.getString(2);
			value[10] = new Integer(us.getInt(4));
			value[11] = new Integer(us.getInt(3));

			rs.addTuple(value);
		}
		us.close();
		endTransaction();

		rs.sortTuples(new CUBRIDComparator("getIndexInfo"));
		return rs;
	}

	public synchronized boolean supportsResultSetType(int type)
			throws SQLException {
		checkIsOpen();
		if (type == ResultSet.TYPE_FORWARD_ONLY)
			return true;
		if (type == ResultSet.TYPE_SCROLL_INSENSITIVE)
			return true;
		if (type == ResultSet.TYPE_SCROLL_SENSITIVE)
			return true;
		return false;
	}

	public synchronized boolean supportsResultSetConcurrency(int type,
			int concurrency) throws SQLException {
		checkIsOpen();
		if (type == ResultSet.TYPE_FORWARD_ONLY
				&& concurrency == ResultSet.CONCUR_READ_ONLY)
			return true;
		if (type == ResultSet.TYPE_FORWARD_ONLY
				&& concurrency == ResultSet.CONCUR_UPDATABLE)
			return true;
		if (type == ResultSet.TYPE_SCROLL_INSENSITIVE
				&& concurrency == ResultSet.CONCUR_READ_ONLY)
			return true;
		if (type == ResultSet.TYPE_SCROLL_INSENSITIVE
				&& concurrency == ResultSet.CONCUR_UPDATABLE)
			return true;
		if (type == ResultSet.TYPE_SCROLL_SENSITIVE
				&& concurrency == ResultSet.CONCUR_READ_ONLY)
			return true;
		if (type == ResultSet.TYPE_SCROLL_SENSITIVE
				&& concurrency == ResultSet.CONCUR_UPDATABLE)
			return true;
		return false;
	}

	public synchronized boolean ownUpdatesAreVisible(int type)
			throws SQLException {
		checkIsOpen();
		if (type == ResultSet.TYPE_SCROLL_SENSITIVE)
			return true;
		return false;
	}

	public synchronized boolean ownDeletesAreVisible(int type)
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean ownInsertsAreVisible(int type)
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean othersUpdatesAreVisible(int type)
			throws SQLException {
		checkIsOpen();
		if (type == ResultSet.TYPE_SCROLL_SENSITIVE)
			return true;
		return false;
	}

	public synchronized boolean othersDeletesAreVisible(int type)
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean othersInsertsAreVisible(int type)
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean updatesAreDetected(int type)
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean deletesAreDetected(int type)
			throws SQLException {
		checkIsOpen();
		if (type == ResultSet.TYPE_SCROLL_SENSITIVE)
			return true;
		return false;
	}

	public synchronized boolean insertsAreDetected(int type)
			throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsBatchUpdates() throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized ResultSet getUDTs(String catalog, String schemaPattern,
			String typeNamePattern, int[] types) throws SQLException {
		throw new UnsupportedOperationException();
	}

	public synchronized Connection getConnection() throws SQLException {
		checkIsOpen();
		return con;
	}

	// 3.0
	public synchronized ResultSet getAttributes(String catalog,
			String schemaPattern, String typeNamePattern,
			String attributeNamePattern) throws SQLException {
		checkIsOpen();

		String[] names = { "TYPE_CAT", "TYPE_SCHEM", "TYPE_NAME", "ATTR_NAME",
				"DATA_TYPE", "ATTR_TYPE_NAME", "ATTR_SIZE", "DECIMAL_DIGITS",
				"NUM_PREC_RADIX", "NULLABLE", "REMARKS", "ATTR_DEF",
				"SQL_DATA_TYPE", "SQL_DATETIME_SUB", "CHAR_OCTET_LENGTH",
				"ORDINAL_POSITION", "IS_NULLABLE", "SCOPE_CATALOG",
				"SCOPE_SCHEMA", "SCOPE_TABLE", "SOURCE_DATA_TYPE" };
		int[] types = { UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_INT, UUType.U_TYPE_VARCHAR, UUType.U_TYPE_INT,
				UUType.U_TYPE_INT, UUType.U_TYPE_INT, UUType.U_TYPE_INT,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_INT, UUType.U_TYPE_INT, UUType.U_TYPE_INT,
				UUType.U_TYPE_INT, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_SHORT };
		boolean[] nullable = { true, true, false, false, false, false, false,
				false, false, false, true, true, false, false, false, false,
				false, false, false, false, false };

		return new CUBRIDResultSetWithoutQuery(21, types, names, nullable, null);
	}

	public synchronized int getDatabaseMajorVersion() throws SQLException {
		checkIsOpen();
		if (this.major_version == -1) {
			getDatabaseProductVersion();
		}
		return this.major_version;
	}

	public synchronized int getDatabaseMinorVersion() throws SQLException {
		checkIsOpen();
		if (this.minor_version == -1) {
			getDatabaseProductVersion();
		}
		return this.minor_version;
	}

	public synchronized int getJDBCMajorVersion() throws SQLException {
		checkIsOpen();
		return 3;
	}

	public synchronized int getJDBCMinorVersion() throws SQLException {
		checkIsOpen();
		return 0;
	}

	public synchronized int getResultSetHoldability() throws SQLException {
		checkIsOpen();
		return con.getHoldability();
	}

	public synchronized int getSQLStateType() throws SQLException {
		checkIsOpen();
		return DatabaseMetaData.sqlStateSQL99;
	}

	public synchronized boolean locatorsUpdateCopy() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsGetGeneratedKeys() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsMultipleOpenResults()
			throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsNamedParameters() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized boolean supportsResultSetHoldability(int holdability)
			throws SQLException {
		checkIsOpen();
		if (holdability == ResultSet.CLOSE_CURSORS_AT_COMMIT
				|| holdability == ResultSet.HOLD_CURSORS_OVER_COMMIT)
			return true;
		return false;
	}

	public synchronized boolean supportsSavepoints() throws SQLException {
		checkIsOpen();
		return true;
	}

	public synchronized boolean supportsStatementPooling() throws SQLException {
		checkIsOpen();
		return false;
	}

	public synchronized ResultSet getSuperTables(String catalog,
			String schemaPattern, String tableNamePattern) throws SQLException {
		checkIsOpen();

		String[] names = { "TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME",
				"SUPERTABLE_NAME" };
		int[] types = { UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR };
		boolean[] nullable = { true, true, false, false };

		CUBRIDResultSetWithoutQuery rs = new CUBRIDResultSetWithoutQuery(4,
				types, names, nullable, null);

		UStatement us = null;
		synchronized (u_con) {
			us = u_con.getSchemaInfo(USchType.SCH_DIRECT_SUPER_CLASS,
					tableNamePattern, null, (byte) 3, shard_id);
			error = u_con.getRecentError();
			switch (error.getErrorCode()) {
			case UErrorCode.ER_NO_ERROR:
				break;
			case UErrorCode.ER_IS_CLOSED:
				close();
				throw new CUBRIDException(CUBRIDJDBCErrorCode.dbmetadata_closed);
			default:
				throw con.createCUBRIDException(error);
			}
		}

		Object[] value = new Object[4];

		value[0] = null;
		value[1] = null;

		int i = 0;
		while (true) {
			us.moveCursor(i++, UStatement.CURSOR_SET);
			if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR)
				break;
			us.fetch();

			value[2] = us.getString(0);
			value[3] = us.getString(1);

			rs.addTuple(value);
		}
		us.close();
		endTransaction();

		rs.sortTuples(new CUBRIDComparator("getSuperTables"));
		return rs;
	}

	public synchronized ResultSet getSuperTypes(String catalog,
			String schemaPattern, String typeNamePattern) throws SQLException {
		checkIsOpen();

		String[] names = { "TYPE_CAT", "TYPE_SCHEM", "TYPE_NAME",
				"SUPERTYPE_CAT", "SUPERTYPE_SCHEM", "SUPERTYPE_NAME" };
		int[] types = { UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR,
				UUType.U_TYPE_VARCHAR, UUType.U_TYPE_VARCHAR };
		boolean[] nullable = { true, true, false, true, true, false };

		return new CUBRIDResultSetWithoutQuery(6, types, names, nullable, null);
	}

	// 3.0

	synchronized void close() {
		is_closed = true;
		con = null;
		u_con = null;
		error = null;
		shard_id = UShardInfo.SHARD_ID_INVALID;
	}

	private void checkIsOpen() throws SQLException {
		if (is_closed) {
			if (con != null) {
				throw con.createCUBRIDException(CUBRIDJDBCErrorCode.dbmetadata_closed, null);
			} else {
				throw new CUBRIDException(CUBRIDJDBCErrorCode.dbmetadata_closed, null);
			}
		}
	}

	private synchronized void endTransaction() {
		if (u_con.getAutoCommit() == true) {
			u_con.endTransaction(true);
		}
	}

	private boolean containsWildcard(String s) {
	  return (s != null && (s.indexOf ('%') >= 0 || s.indexOf ('_') >= 0));
	}

    public synchronized void setShardId(int sid) throws SQLException {
		checkIsOpen();
		shard_id = sid;
	}

    public synchronized int getShardId() throws SQLException {
		checkIsOpen();
		return shard_id;
	}

	public synchronized String getShardDBName() throws SQLException {
		UShardInfo shard_info;

		checkIsOpen();

		if (con.isShard() == false)
		{
			return null;
		}

		shard_info = u_con.getShardInfo(shard_id);
		error = u_con.getRecentError();
		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}

		return shard_info.getDBName();
	}

	public synchronized String getShardDBServer() throws SQLException {
		UShardInfo shard_info;

		checkIsOpen();

		if (con.isShard() == false)
		{
			return null;
		}

		shard_info = u_con.getShardInfo(shard_id);
		error = u_con.getRecentError();
		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}

		return shard_info.getDBServer();
	}

	/* JDK 1.6 */
	public boolean autoCommitFailureClosesAllResultSets() throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public ResultSet getClientInfoProperties() throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public ResultSet getFunctionColumns(String catalog, String schemaPattern,
			String functionNamePattern, String columnNamePattern)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public ResultSet getFunctions(String catalog, String schemaPattern,
			String functionNamePattern) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public RowIdLifetime getRowIdLifetime() throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public ResultSet getSchemas(String catalog, String schemaPattern)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public boolean supportsStoredFunctionsUsingCallSyntax() throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public boolean isWrapperFor(Class<?> iface) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public <T> T unwrap(Class<T> iface) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.7 */
	public ResultSet getPseudoColumns(String catalog, String schemaPattern,
			String tableNamePattern, String columnNamePattern)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.7 */
	public boolean generatedKeyAlwaysReturned() throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

}
