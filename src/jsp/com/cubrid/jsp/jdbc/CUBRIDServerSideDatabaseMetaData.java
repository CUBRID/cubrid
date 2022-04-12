/*
 * Copyright (C) 2008 Search Solution Corporation.
 * Copyright (c) 2016 CUBRID Corporation.
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

package com.cubrid.jsp.jdbc;

import com.cubrid.jsp.data.ColumnInfo;
import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.impl.SUStatement;
import cubrid.jdbc.driver.CUBRIDDriver;
import cubrid.jdbc.jci.USchType;
import java.sql.Connection;
import java.sql.DatabaseMetaData;
import java.sql.ResultSet;
import java.sql.RowIdLifetime;
import java.sql.SQLException;
import java.util.List;
import java.util.StringTokenizer;

/**
 * Title: CUBRID JDBC Driver Description:
 *
 * @version 2.0
 */
public class CUBRIDServerSideDatabaseMetaData implements DatabaseMetaData {
    private static final int SQL_MAX_CHAR_LITERAL_LEN = 1073741823;

    CUBRIDServerSideConnection con;
    boolean isClosed;

    String productVersion;
    int majorVersion;
    int minorVersion;

    protected CUBRIDServerSideDatabaseMetaData(CUBRIDServerSideConnection c) {
        con = c;
        isClosed = false;

        productVersion = null;
        majorVersion = -1;
        minorVersion = -1;
    }

    protected void parserVersionString () {
        productVersion = System.getProperty("cubrid.server.version");

        StringTokenizer st = new StringTokenizer(productVersion, ".");
        if (st.countTokens() == 4) { // ex) 8.4.9.9999(major.minor.patch.build
            this.majorVersion = Integer.parseInt(st.nextToken());
            this.minorVersion = Integer.parseInt(st.nextToken());
        }
    }

    // ==============================================================
    // The following is JDBC Interface Implementations
    // ==============================================================

    @Override
    public boolean allProceduresAreCallable() throws SQLException {
        return false;
    }

    @Override
    public boolean allTablesAreSelectable() throws SQLException {
        return false;
    }

    @Override
    public String getURL() throws SQLException {
        // TODO: It will be always localhost?
        return "localhost";
    }

    @Override
    public String getUserName() throws SQLException {
        // TODO
        // return con.user;
        return "CUBRID";
    }

    @Override
    public boolean isReadOnly() throws SQLException {
        return false;
    }

    @Override
    public boolean nullsAreSortedHigh() throws SQLException {
        return false;
    }

    @Override
    public boolean nullsAreSortedLow() throws SQLException {
        return true;
    }

    @Override
    public boolean nullsAreSortedAtStart() throws SQLException {
        return false;
    }

    @Override
    public boolean nullsAreSortedAtEnd() throws SQLException {
        return false;
    }

    @Override
    public String getDatabaseProductName() throws SQLException {
        return "CUBRID";
    }

    @Override
    public String getDatabaseProductVersion() throws SQLException {
        if (productVersion == null) {
            parserVersionString ();
        }
        return productVersion;
    }

    @Override
    public String getDriverName() throws SQLException {
        return "CUBRID Internal JDBC Driver";
    }

    @Override
    public String getDriverVersion() throws SQLException {
        return CUBRIDDriver.version_string;
    }

    @Override
    public int getDriverMajorVersion() {
        return CUBRIDDriver.major_version;
    }

    @Override
    public int getDriverMinorVersion() {
        return CUBRIDDriver.minor_version;
    }

    @Override
    public boolean usesLocalFiles() throws SQLException {
        return false;
    }

    @Override
    public boolean usesLocalFilePerTable() throws SQLException {
        return false;
    }

    @Override
    public boolean supportsMixedCaseIdentifiers() throws SQLException {
        return false;
    }

    @Override
    public boolean storesUpperCaseIdentifiers() throws SQLException {
        return false;
    }

    @Override
    public boolean storesLowerCaseIdentifiers() throws SQLException {
        return true;
    }

    @Override
    public boolean storesMixedCaseIdentifiers() throws SQLException {
        return false;
    }

    @Override
    public boolean supportsMixedCaseQuotedIdentifiers() throws SQLException {
        return false;
    }

    @Override
    public boolean storesUpperCaseQuotedIdentifiers() throws SQLException {
        return false;
    }

    @Override
    public boolean storesLowerCaseQuotedIdentifiers() throws SQLException {
        return false;
    }

    @Override
    public boolean storesMixedCaseQuotedIdentifiers() throws SQLException {
        return false;
    }

    @Override
    public String getIdentifierQuoteString() throws SQLException {
        return "\"";
    }

    @Override
    public String getSQLKeywords() throws SQLException {
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

    @Override
    public String getNumericFunctions() throws SQLException {
        return "AVG, COUNT, MAX, MIN, STDDEV, SUM, VARIANCE";
    }

    @Override
    public String getStringFunctions() throws SQLException {
        return "BIT_LENGTH, CHAR_LENGTH, LOWER, LTRIM, OCTET_LENGTH, POSITION, REPLACE, "
                + "RPAD, RTRIM, SUBSTRING, TRANSLATE, TRIM, TO_CHAR, TO_DATE, TO_NUMBER, "
                + "TO_TIME, TO_TIMESTAMP, TO_DATETIME, UPPER";
    }

    @Override
    public String getSystemFunctions() throws SQLException {
        return "";
    }

    @Override
    public String getTimeDateFunctions() throws SQLException {
        return "ADD_MONTHS, LAST_DAY, MONTH_BETWEEN, SYS_DATE, SYS_TIME, SYS_TIMESTMAP, TO_DATE, TO_TIME, TO_TIMESTAMP, TO_DATETIME";
    }

    @Override
    public String getSearchStringEscape() throws SQLException {
        return null;
    }

    @Override
    public String getExtraNameCharacters() throws SQLException {
        return "%#";
    }

    @Override
    public boolean supportsAlterTableWithAddColumn() throws SQLException {
        return true;
    }

    @Override
    public boolean supportsAlterTableWithDropColumn() throws SQLException {
        return true;
    }

    @Override
    public boolean supportsColumnAliasing() throws SQLException {
        return false;
    }

    @Override
    public boolean nullPlusNonNullIsNull() throws SQLException {
        return true;
    }

    @Override
    public boolean supportsConvert() throws SQLException {
        return false;
    }

    @Override
    public boolean supportsConvert(int fromType, int toType) throws SQLException {
        return false;
    }

    @Override
    public boolean supportsTableCorrelationNames() throws SQLException {
        return true;
    }

    @Override
    public boolean supportsDifferentTableCorrelationNames() throws SQLException {
        return false;
    }

    @Override
    public boolean supportsExpressionsInOrderBy() throws SQLException {
        return false;
    }

    @Override
    public boolean supportsOrderByUnrelated() throws SQLException {
        return false;
    }

    @Override
    public boolean supportsGroupBy() throws SQLException {
        return true;
    }

    @Override
    public boolean supportsGroupByUnrelated() throws SQLException {
        return false;
    }

    @Override
    public boolean supportsGroupByBeyondSelect() throws SQLException {
        return true;
    }

    @Override
    public boolean supportsLikeEscapeClause() throws SQLException {
        return true;
    }

    @Override
    public boolean supportsMultipleResultSets() throws SQLException {
        return true;
    }

    @Override
    public boolean supportsMultipleTransactions() throws SQLException {
        return true;
    }

    @Override
    public boolean supportsNonNullableColumns() throws SQLException {
        return true;
    }

    @Override
    public boolean supportsMinimumSQLGrammar() throws SQLException {

        return true;
    }

    @Override
    public boolean supportsCoreSQLGrammar() throws SQLException {

        return true;
    }

    @Override
    public boolean supportsExtendedSQLGrammar() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsANSI92EntryLevelSQL() throws SQLException {

        return true;
    }

    @Override
    public boolean supportsANSI92IntermediateSQL() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsANSI92FullSQL() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsIntegrityEnhancementFacility() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsOuterJoins() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsFullOuterJoins() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsLimitedOuterJoins() throws SQLException {

        return false;
    }

    @Override
    public String getSchemaTerm() throws SQLException {

        return "";
    }

    @Override
    public String getProcedureTerm() throws SQLException {

        return "";
    }

    @Override
    public String getCatalogTerm() throws SQLException {

        return null;
    }

    @Override
    public boolean isCatalogAtStart() throws SQLException {

        return true;
    }

    @Override
    public String getCatalogSeparator() throws SQLException {

        return null;
    }

    @Override
    public boolean supportsSchemasInDataManipulation() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsSchemasInProcedureCalls() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsSchemasInTableDefinitions() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsSchemasInIndexDefinitions() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsSchemasInPrivilegeDefinitions() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsCatalogsInDataManipulation() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsCatalogsInProcedureCalls() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsCatalogsInTableDefinitions() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsCatalogsInIndexDefinitions() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsCatalogsInPrivilegeDefinitions() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsPositionedDelete() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsPositionedUpdate() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsSelectForUpdate() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsStoredProcedures() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsSubqueriesInComparisons() throws SQLException {

        return true;
    }

    @Override
    public boolean supportsSubqueriesInExists() throws SQLException {

        return true;
    }

    @Override
    public boolean supportsSubqueriesInIns() throws SQLException {

        return true;
    }

    @Override
    public boolean supportsSubqueriesInQuantifieds() throws SQLException {

        return true;
    }

    @Override
    public boolean supportsCorrelatedSubqueries() throws SQLException {

        return true;
    }

    @Override
    public boolean supportsUnion() throws SQLException {

        return true;
    }

    @Override
    public boolean supportsUnionAll() throws SQLException {

        return true;
    }

    @Override
    public boolean supportsOpenCursorsAcrossCommit() throws SQLException {

        return true;
    }

    @Override
    public boolean supportsOpenCursorsAcrossRollback() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsOpenStatementsAcrossCommit() throws SQLException {

        return false;
    }

    @Override
    public boolean supportsOpenStatementsAcrossRollback() throws SQLException {

        return false;
    }

    @Override
    public int getMaxBinaryLiteralLength() throws SQLException {

        return (SQL_MAX_CHAR_LITERAL_LEN / 8);
    }

    @Override
    public int getMaxCharLiteralLength() throws SQLException {

        return SQL_MAX_CHAR_LITERAL_LEN;
    }

    @Override
    public int getMaxColumnNameLength() throws SQLException {

        return 254;
    }

    @Override
    public int getMaxColumnsInGroupBy() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxColumnsInIndex() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxColumnsInOrderBy() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxColumnsInSelect() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxColumnsInTable() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxConnections() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxCursorNameLength() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxIndexLength() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxSchemaNameLength() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxProcedureNameLength() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxCatalogNameLength() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxRowSize() throws SQLException {

        return 0;
    }

    @Override
    public boolean doesMaxRowSizeIncludeBlobs() throws SQLException {

        return false;
    }

    @Override
    public int getMaxStatementLength() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxStatements() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxTableNameLength() throws SQLException {

        return 254;
    }

    @Override
    public int getMaxTablesInSelect() throws SQLException {

        return 0;
    }

    @Override
    public int getMaxUserNameLength() throws SQLException {

        return 31;
    }

    @Override
    public int getDefaultTransactionIsolation() throws SQLException {

        return Connection.TRANSACTION_READ_COMMITTED;
    }

    @Override
    public boolean supportsTransactions() throws SQLException {

        return true;
    }

    @Override
    public boolean supportsTransactionIsolationLevel(int level) throws SQLException {
        switch (level) {
            case Connection.TRANSACTION_READ_COMMITTED:
            case Connection.TRANSACTION_REPEATABLE_READ:
            case Connection.TRANSACTION_SERIALIZABLE:
            // case CUBRIDServerSideConnection.TRAN_REP_CLASS_COMMIT_INSTANCE:
                return true;
            default:
                return false;
        }
    }

    @Override
    public boolean supportsDataDefinitionAndDataManipulationTransactions()
            throws SQLException {
        return true;
    }

    @Override
    public boolean supportsDataManipulationTransactionsOnly() throws SQLException {
        return true;
    }

    @Override
    public boolean dataDefinitionCausesTransactionCommit() throws SQLException {
        return false;
    }

    @Override
    public boolean dataDefinitionIgnoredInTransactions() throws SQLException {
        return false;
    }

    /*
     * empty ResultSet
     */
    @Override
    public ResultSet getProcedures(
            String catalog, String schemaPattern, String procedureNamePattern) throws SQLException {
        String[] names = {
            "PROCEDURE_CAT",
            "PROCEDURE_SCHEM",
            "PROCEDURE_NAME",
            "",
            "",
            "",
            "REMARKS",
            "PROCEDURE_TYPE"
        };

        int[] types = {
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_SHORT,
        };

        boolean[] nullable = {true, true, false, true, true, true, false, false};
        
        // TODO
        return null;
    }

    /*
     * empty ResultSet
     */
    @Override
    public ResultSet getProcedureColumns(
            String catalog,
            String schemaPattern,
            String procedureNamePattern,
            String columnNamePattern)
            throws SQLException {
        // TODO

        String[] names = {
            "PROCEDURE_CAT",
            "PROCEDURE_SCHEM",
            "PROCEDURE_NAME",
            "COLUMN_NAME",
            "COLUMN_TYPE",
            "DATA_TYPE",
            "TYPE_NAME",
            "PRECISION",
            "LENGTH",
            "SCALE",
            "RADIX",
            "NULLABLE",
            "REMARKS"
        };

        int[] types = {
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_SHORT,
            DBType.DB_SHORT,
            DBType.DB_STRING,
            DBType.DB_INT,
            DBType.DB_INT,
            DBType.DB_SHORT,
            DBType.DB_SHORT,
            DBType.DB_SHORT,
            DBType.DB_STRING,
        };
        
        boolean[] nullable = {
            true, true, false, false, false, false, false, false, false, false, false, false, false
        };
        
        // TODO
        return null;
    }

    @Override
    public ResultSet getTables(
            String catalog, String schemaPattern, String tableNamePattern, String[] types)
            throws SQLException {
                
        String[] names = {"TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "TABLE_TYPE", "REMARKS"};
        int[] type = {
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
        };
        boolean[] nullable = {true, true, false, false, false};
        
        SUStatement s = null;
        try {
        s = con.getSUConnection().getSchemaInfo(
            USchType.SCH_CLASS, tableNamePattern, null, (byte) 3);
        } catch (Exception e) {
            throw new SQLException (e);
        }

        List<ColumnInfo> columnInfo = s.getColumnInfo();
        boolean has_remarks = false;
        if (columnInfo.size() > 2) {
            /*
             * Class schema info may have two types of column info:
             * i - [0]=TABLE_NAME, [1]=TABLE_TYPE;
             * ii- [0]=TABLE_NAME, [1]=TABLE_TYPE, [2]=REMARKS;
             */
            has_remarks = true;
        }
        
        int[] precision = new int[5];
        precision[0] = 0; /* TABLE_CAT */
        precision[1] = 0; /* TABLE_SCHEM */
        precision[2] = columnInfo.get(0).prec; /* TABLE_NAME */
        precision[3] = 12; /* TABLE_TYPE */
        precision[4] = has_remarks ? columnInfo.get(2).prec : 0; /* REMARKS */
        
        // TODO

        Object[] value = new Object[5];
        value[0] = null;
        value[1] = null;
        if (has_remarks == false) {
            value[4] = null;
        }

        // TABLE type
        int j = 0;
        if (types != null) {
            for (j = 0; j < types.length; j++) if (types[j].equalsIgnoreCase("TABLE")) break;
        }

        /*
        if (types == null || j < types.length) {
            value[3] = "TABLE";

            int i = 0;
            while (true) {
                us.moveCursor(i++, UStatement.CURSOR_SET);
                if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR) break;
                us.fetch();

                if (us.getInt(1) != 2) continue;

                value[2] = us.getString(0);
                if (has_remarks == true) {
                    value[4] = us.getString(2);
                }
                rs.addTuple(value);
            }
        }

        // VIEW type
        if (types != null) {
            for (j = 0; j < types.length; j++) {
                if (types[j].equalsIgnoreCase("VIEW")) break;
            }
        }

        if (types == null || j < types.length) {
            value[3] = "VIEW";

            int i = 0;
            while (true) {
                us.moveCursor(i++, UStatement.CURSOR_SET);
                if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR) break;
                us.fetch();

                if (us.getInt(1) != 1) continue;

                value[2] = us.getString(0);
                if (has_remarks == true) {
                    value[4] = us.getString(2);
                }
                rs.addTuple(value);
            }
        }

        // SYSTEM TABLE type
        if (types != null) {
            for (j = 0; j < types.length; j++) if (types[j].equalsIgnoreCase("SYSTEM TABLE")) break;
        }

        if (types == null || j < types.length) {
            value[3] = "SYSTEM TABLE";

            int i = 0;
            while (true) {
                us.moveCursor(i++, UStatement.CURSOR_SET);
                if (us.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR) break;
                us.fetch();

                if (us.getInt(1) != 0) continue;

                value[2] = us.getString(0);
                if (has_remarks == true) {
                    value[4] = us.getString(2);
                }
                rs.addTuple(value);
            }
        }
        */

        // TODO
        return null;
    }
    
    /*
     * empty ResultSet
     */
    @Override
    public ResultSet getSchemas() throws SQLException {
        String[] names = {"TABLE_SCHEM"};
        int[] types = {DBType.DB_STRING};
        boolean[] nullable = {false};

        // TODO
        return null;
    }

    /*
     * empty ResultSet
     */
    @Override
    public ResultSet getCatalogs() throws SQLException {
        
        String[] names = {"TABLE_SCHEM"};
        int[] types = {DBType.DB_STRING};
        boolean[] nullable = {false};
        // TODO
        
        return null;
    }

    @Override
    public ResultSet getTableTypes() throws SQLException {
        // TODO
        return null;
    }

    @Override
    public ResultSet getColumns(
            String catalog, String schemaPattern, String tableNamePattern, String columnNamePattern)
            throws SQLException {
        // TODO
        return null;
    }

    @Override
    public ResultSet getColumnPrivileges(
            String catalog, String schema, String table, String columnNamePattern)
            throws SQLException {
        // TODO
        return null;
    }

    @Override
    public ResultSet getTablePrivileges(
            String catalog, String schemaPattern, String tableNamePattern) throws SQLException {
        // TODO
        return null;
    }

    @Override
    public ResultSet getBestRowIdentifier(
            String catalog, String schema, String table, int scope, boolean nullable)
            throws SQLException {
        // TODO
        return null;
    }

    /*
     * empty ResultSet
     */
    @Override
    public ResultSet getVersionColumns(String catalog, String schema, String table)
            throws SQLException {
        // TODO
        return null;
    }

    @Override
    public ResultSet getPrimaryKeys(String catalog, String schema, String table) {
        // TODO
        return null;
    }

    private ResultSet getForeignKeys(int type, String table1, String table2) throws SQLException {
        // TODO
        return null;
    }

    @Override
    public ResultSet getImportedKeys(String catalog, String schema, String table)
            throws SQLException {
        if (table == null) {
            // TODO: error handling
        }
        return getForeignKeys(USchType.SCH_IMPORTED_KEYS, table, null);
    }

    @Override
    public ResultSet getExportedKeys(String catalog, String schema, String table)
            throws SQLException {
        if (table == null) {
            // TODO: error handling
            
        }
        return getForeignKeys(USchType.SCH_EXPORTED_KEYS, table, null);
    }

    @Override
    public ResultSet getCrossReference(
            String primaryCatalog,
            String primarySchema,
            String primaryTable,
            String foreignCatalog,
            String foreignSchema,
            String foreignTable)
            throws SQLException {

        if (primaryTable == null || foreignTable == null) {
            // TODO: error handling
        }
        return getForeignKeys(USchType.SCH_CROSS_REFERENCE, primaryTable, foreignTable);
    }

    @Override
    public ResultSet getTypeInfo() throws SQLException {
        String[] names = {
            "TYPE_NAME",
            "DATA_TYPE",
            "PRECISION",
            "LITERAL_PREFIX",
            "LITERAL_SUFFIX",
            "CREATE_PARAMS",
            "NULLABLE",
            "CASE_SENSITIVE",
            "SEARCHABLE",
            "UNSIGNED_ATTRIBUTE",
            "FIXED_PREC_SCALE",
            "AUTO_INCREMENT",
            "LOCAL_TYPE_NAME",
            "MINIMUM_SCALE",
            "MAXIMUM_SCALE",
            "SQL_DATA_TYPE",
            "SQL_DATETIME_SUB",
            "NUM_PREC_RADIX"
        };
        
        int[] types = {
            DBType.DB_STRING,
            DBType.DB_SHORT,
            DBType.DB_INT,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_SHORT,
            DBType.DB_BIT,
            DBType.DB_SHORT,
            DBType.DB_BIT,
            DBType.DB_BIT,
            DBType.DB_BIT,
            DBType.DB_STRING,
            DBType.DB_SHORT,
            DBType.DB_SHORT,
            DBType.DB_INT,
            DBType.DB_INT,
            DBType.DB_INT
        };

        boolean[] nullable = {
            false, false, false, true, true, true, false, false, false, false, false, false, true,
            false, false, true, true, false
        };

        // TODO
        return null;
    }

    @Override
    public ResultSet getIndexInfo(
            String catalog, String schema, String table, boolean unique, boolean approximate)
            throws SQLException {
        String[] names = {
            "TABLE_CAT",
            "TABLE_SCHEM",
            "TABLE_NAME",
            "NON_UNIQUE",
            "INDEX_QUALIFIER",
            "INDEX_NAME",
            "TYPE",
            "ORDINAL_POSITION",
            "COLUMN_NAME",
            "ASC_OR_DESC",
            "CARDINALITY",
            "PAGES",
            "FILTER_CONDITION"
        };

        int[] types = {
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_BIT,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_SHORT,
            DBType.DB_SHORT,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_INT,
            DBType.DB_INT,
            DBType.DB_STRING
        };

        boolean[] nullable = {
            true, true, false, false, true, false, false, false, false, false, false, false, true
        };

        // TODO
        return null;
    }

    @Override
    public boolean supportsResultSetType(int type) throws SQLException {
        if (type == ResultSet.TYPE_FORWARD_ONLY) return true;
        if (type == ResultSet.TYPE_SCROLL_INSENSITIVE) return true;
        if (type == ResultSet.TYPE_SCROLL_SENSITIVE) return true;
        return false;
    }

    @Override
    public boolean supportsResultSetConcurrency(int type, int concurrency) throws SQLException {
        if (type == ResultSet.TYPE_FORWARD_ONLY && concurrency == ResultSet.CONCUR_READ_ONLY)
            return true;
        if (type == ResultSet.TYPE_FORWARD_ONLY && concurrency == ResultSet.CONCUR_UPDATABLE)
            return true;
        if (type == ResultSet.TYPE_SCROLL_INSENSITIVE && concurrency == ResultSet.CONCUR_READ_ONLY)
            return true;
        if (type == ResultSet.TYPE_SCROLL_INSENSITIVE && concurrency == ResultSet.CONCUR_UPDATABLE)
            return true;
        if (type == ResultSet.TYPE_SCROLL_SENSITIVE && concurrency == ResultSet.CONCUR_READ_ONLY)
            return true;
        if (type == ResultSet.TYPE_SCROLL_SENSITIVE && concurrency == ResultSet.CONCUR_UPDATABLE)
            return true;
        return false;
    }

    @Override
    public boolean ownUpdatesAreVisible(int type) throws SQLException {
        if (type == ResultSet.TYPE_SCROLL_SENSITIVE) return true;
        return false;
    }

    @Override
    public boolean ownDeletesAreVisible(int type) throws SQLException {
        return false;
    }

    @Override
    public boolean ownInsertsAreVisible(int type) throws SQLException {
        return false;
    }

    @Override
    public boolean othersUpdatesAreVisible(int type) throws SQLException {
        if (type == ResultSet.TYPE_SCROLL_SENSITIVE) return true;
        return false;
    }

    @Override
    public boolean othersDeletesAreVisible(int type) throws SQLException {
        return false;
    }

    @Override
    public boolean othersInsertsAreVisible(int type) throws SQLException {
        return false;
    }

    @Override
    public boolean updatesAreDetected(int type) throws SQLException {
        return false;
    }

    @Override
    public boolean deletesAreDetected(int type) throws SQLException {
        if (type == ResultSet.TYPE_SCROLL_SENSITIVE) return true;
        return false;
    }

    @Override
    public boolean insertsAreDetected(int type) throws SQLException {
        return false;
    }

    @Override
    public boolean supportsBatchUpdates() throws SQLException {
        return true;
    }

    @Override
    public ResultSet getUDTs(
            String catalog, String schemaPattern, String typeNamePattern, int[] types)
            throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public Connection getConnection() throws SQLException {
        return con;
    }

    // 3.0
    @Override
    public ResultSet getAttributes(
            String catalog,
            String schemaPattern,
            String typeNamePattern,
            String attributeNamePattern)
            throws SQLException {

        String[] names = {
            "TYPE_CAT",
            "TYPE_SCHEM",
            "TYPE_NAME",
            "ATTR_NAME",
            "DATA_TYPE",
            "ATTR_TYPE_NAME",
            "ATTR_SIZE",
            "DECIMAL_DIGITS",
            "NUM_PREC_RADIX",
            "NULLABLE",
            "REMARKS",
            "ATTR_DEF",
            "SQL_DATA_TYPE",
            "SQL_DATETIME_SUB",
            "CHAR_OCTET_LENGTH",
            "ORDINAL_POSITION",
            "IS_NULLABLE",
            "SCOPE_CATALOG",
            "SCOPE_SCHEMA",
            "SCOPE_TABLE",
            "SOURCE_DATA_TYPE"
        };

        int[] types = {
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_INT,
            DBType.DB_STRING,
            DBType.DB_INT,
            DBType.DB_INT,
            DBType.DB_INT,
            DBType.DB_INT,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_INT,
            DBType.DB_INT,
            DBType.DB_INT,
            DBType.DB_INT,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_SHORT
        };

        boolean[] nullable = {
            true, true, false, false, false, false, false, false, false, false, true, true, false,
            false, false, false, false, false, false, false, false
        };

        // TODO

        return null;
    }

    @Override
    public int getDatabaseMajorVersion() throws SQLException {
        if (majorVersion == -1) {
            parserVersionString ();
        }
        return majorVersion;
    }

    @Override
    public int getDatabaseMinorVersion() throws SQLException {
        if (minorVersion == -1) {
            parserVersionString ();
        }
        return minorVersion;
    }

    @Override
    public int getJDBCMajorVersion() throws SQLException {
        return 3;
    }

    @Override
    public int getJDBCMinorVersion() throws SQLException {

        return 0;
    }

    @Override
    public int getResultSetHoldability() throws SQLException {
        return con.getHoldability();
    }

    @Override
    public int getSQLStateType() throws SQLException {
        return DatabaseMetaData.sqlStateSQL99;
    }

    @Override
    public boolean locatorsUpdateCopy() throws SQLException {
        return false;
    }

    @Override
    public boolean supportsGetGeneratedKeys() throws SQLException {
        return false;
    }

    @Override
    public boolean supportsMultipleOpenResults() throws SQLException {
        return true;
    }

    @Override
    public boolean supportsNamedParameters() throws SQLException {
        return false;
    }

    @Override
    public boolean supportsResultSetHoldability(int holdability) throws SQLException {
        if (holdability == ResultSet.CLOSE_CURSORS_AT_COMMIT
                || holdability == ResultSet.HOLD_CURSORS_OVER_COMMIT) return true;
        return false;
    }

    @Override
    public boolean supportsSavepoints() throws SQLException {
        return true;
    }

    @Override
    public boolean supportsStatementPooling() throws SQLException {
        return false;
    }

    @Override
    public ResultSet getSuperTables(
            String catalog, String schemaPattern, String tableNamePattern) throws SQLException {

        String[] names = {"TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "SUPERTABLE_NAME"};

        int[] type = {
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING
        };

        boolean[] nullable = {true, true, false, false};

        // TODO
        return null;
    }

    @Override
    public ResultSet getSuperTypes(
            String catalog, String schemaPattern, String typeNamePattern) throws SQLException {
        
        String[] names = {
            "TYPE_CAT",
            "TYPE_SCHEM",
            "TYPE_NAME",
            "SUPERTYPE_CAT",
            "SUPERTYPE_SCHEM",
            "SUPERTYPE_NAME"
        };

        int[] type = {
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
            DBType.DB_STRING,
        };
        
        boolean[] nullable = {true, true, false, true, true, false};
        
        // TODO
        return null;
    }

    // 3.0

    void close() {
        // TODO
        isClosed = true;
        con = null;
    }

    public void setShardId(int sid) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public int getShardId() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public String getShardDBName() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public String getShardDBServer() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public boolean autoCommitFailureClosesAllResultSets() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public ResultSet getClientInfoProperties() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public ResultSet getFunctionColumns(
            String catalog,
            String schemaPattern,
            String functionNamePattern,
            String columnNamePattern)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public ResultSet getFunctions(String catalog, String schemaPattern, String functionNamePattern)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public RowIdLifetime getRowIdLifetime() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public ResultSet getSchemas(String catalog, String schemaPattern) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public boolean supportsStoredFunctionsUsingCallSyntax() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public boolean isWrapperFor(Class<?> iface) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public <T> T unwrap(Class<T> iface) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    @Override
    public ResultSet getPseudoColumns(
            String catalog, String schemaPattern, String tableNamePattern, String columnNamePattern)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    @Override
    public boolean generatedKeyAlwaysReturned() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }
}
