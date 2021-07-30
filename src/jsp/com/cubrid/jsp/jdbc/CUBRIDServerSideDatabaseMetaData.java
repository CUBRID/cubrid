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

import java.sql.Connection;
import java.sql.DatabaseMetaData;
import java.sql.ResultSet;
import java.sql.RowIdLifetime;
import java.sql.SQLException;

/**
 * Title: CUBRID JDBC Driver Description:
 *
 * @version 2.0
 */
public class CUBRIDServerSideDatabaseMetaData implements DatabaseMetaData {
    private static final int SQL_MAX_CHAR_LITERAL_LEN = 1073741823;

    CUBRIDServerSideConnection con;
    boolean isClosed;
    int major_version;
    int minor_version;

    protected CUBRIDServerSideDatabaseMetaData(CUBRIDServerSideConnection c) {
        con = c;
        isClosed = false;
        major_version = -1;
        minor_version = -1;
    }

    // ==============================================================
    // The following is JDBC Interface Implementations
    // ==============================================================

    public boolean allProceduresAreCallable() throws SQLException {
        return false;
    }

    public boolean allTablesAreSelectable() throws SQLException {
        return false;
    }

    public String getURL() throws SQLException {
        // TODO: It will be always localhost?
        return "localhost";
    }

    public String getUserName() throws SQLException {
        // TODO
        // return con.user;
        return "CUBRID";
    }

    public boolean isReadOnly() throws SQLException {
        return false;
    }

    public boolean nullsAreSortedHigh() throws SQLException {
        return false;
    }

    public boolean nullsAreSortedLow() throws SQLException {
        return true;
    }

    public boolean nullsAreSortedAtStart() throws SQLException {
        return false;
    }

    public boolean nullsAreSortedAtEnd() throws SQLException {
        return false;
    }

    public String getDatabaseProductName() throws SQLException {
        return "CUBRID";
    }

    public String getDatabaseProductVersion() throws SQLException {
        String version = System.getProperty("cubrid.server.version");
        return version;
    }

    public String getDriverName() throws SQLException {
        return "CUBRID Server-side JDBC Driver";
    }

    public String getDriverVersion() throws SQLException {
        String version = System.getProperty("cubrid.server.version");
        return version;
    }

    public int getDriverMajorVersion() {
        // TODO
        // return CUBRIDDriver.major_version;
        return 0;
    }

    public int getDriverMinorVersion() {
        // TODO
        // return CUBRIDDriver.minor_version;
        return 0;
    }

    public boolean usesLocalFiles() throws SQLException {
        return false;
    }

    public boolean usesLocalFilePerTable() throws SQLException {
        return false;
    }

    public boolean supportsMixedCaseIdentifiers() throws SQLException {
        return false;
    }

    public boolean storesUpperCaseIdentifiers() throws SQLException {
        return false;
    }

    public boolean storesLowerCaseIdentifiers() throws SQLException {
        return true;
    }

    public boolean storesMixedCaseIdentifiers() throws SQLException {
        return false;
    }

    public boolean supportsMixedCaseQuotedIdentifiers() throws SQLException {
        return false;
    }

    public boolean storesUpperCaseQuotedIdentifiers() throws SQLException {
        return false;
    }

    public boolean storesLowerCaseQuotedIdentifiers() throws SQLException {
        return false;
    }

    public boolean storesMixedCaseQuotedIdentifiers() throws SQLException {
        return false;
    }

    public String getIdentifierQuoteString() throws SQLException {
        return "\"";
    }

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

    public String getNumericFunctions() throws SQLException {
        return "AVG, COUNT, MAX, MIN, STDDEV, SUM, VARIANCE";
    }

    public String getStringFunctions() throws SQLException {
        return "BIT_LENGTH, CHAR_LENGTH, LOWER, LTRIM, OCTET_LENGTH, POSITION, REPLACE, "
                + "RPAD, RTRIM, SUBSTRING, TRANSLATE, TRIM, TO_CHAR, TO_DATE, TO_NUMBER, "
                + "TO_TIME, TO_TIMESTAMP, TO_DATETIME, UPPER";
    }

    public String getSystemFunctions() throws SQLException {
        return "";
    }

    public String getTimeDateFunctions() throws SQLException {
        return "ADD_MONTHS, LAST_DAY, MONTH_BETWEEN, SYS_DATE, SYS_TIME, SYS_TIMESTMAP, TO_DATE, TO_TIME, TO_TIMESTAMP, TO_DATETIME";
    }

    public String getSearchStringEscape() throws SQLException {
        return null;
    }

    public String getExtraNameCharacters() throws SQLException {
        return "%#";
    }

    public boolean supportsAlterTableWithAddColumn() throws SQLException {
        return true;
    }

    public boolean supportsAlterTableWithDropColumn() throws SQLException {
        return true;
    }

    public boolean supportsColumnAliasing() throws SQLException {
        return false;
    }

    public boolean nullPlusNonNullIsNull() throws SQLException {
        return true;
    }

    public boolean supportsConvert() throws SQLException {
        return false;
    }

    public boolean supportsConvert(int fromType, int toType) throws SQLException {
        return false;
    }

    public boolean supportsTableCorrelationNames() throws SQLException {
        return true;
    }

    public boolean supportsDifferentTableCorrelationNames() throws SQLException {
        return false;
    }

    public boolean supportsExpressionsInOrderBy() throws SQLException {
        return false;
    }

    public boolean supportsOrderByUnrelated() throws SQLException {
        return false;
    }

    public boolean supportsGroupBy() throws SQLException {
        return true;
    }

    public boolean supportsGroupByUnrelated() throws SQLException {
        return false;
    }

    public boolean supportsGroupByBeyondSelect() throws SQLException {
        return true;
    }

    public boolean supportsLikeEscapeClause() throws SQLException {
        return true;
    }

    public boolean supportsMultipleResultSets() throws SQLException {
        return true;
    }

    public boolean supportsMultipleTransactions() throws SQLException {
        return true;
    }

    public boolean supportsNonNullableColumns() throws SQLException {
        return true;
    }

    public boolean supportsMinimumSQLGrammar() throws SQLException {

        return true;
    }

    public boolean supportsCoreSQLGrammar() throws SQLException {

        return true;
    }

    public boolean supportsExtendedSQLGrammar() throws SQLException {

        return false;
    }

    public boolean supportsANSI92EntryLevelSQL() throws SQLException {

        return true;
    }

    public boolean supportsANSI92IntermediateSQL() throws SQLException {

        return false;
    }

    public boolean supportsANSI92FullSQL() throws SQLException {

        return false;
    }

    public boolean supportsIntegrityEnhancementFacility() throws SQLException {

        return false;
    }

    public boolean supportsOuterJoins() throws SQLException {

        return false;
    }

    public boolean supportsFullOuterJoins() throws SQLException {

        return false;
    }

    public boolean supportsLimitedOuterJoins() throws SQLException {

        return false;
    }

    public String getSchemaTerm() throws SQLException {

        return "";
    }

    public String getProcedureTerm() throws SQLException {

        return "";
    }

    public String getCatalogTerm() throws SQLException {

        return null;
    }

    public boolean isCatalogAtStart() throws SQLException {

        return true;
    }

    public String getCatalogSeparator() throws SQLException {

        return null;
    }

    public boolean supportsSchemasInDataManipulation() throws SQLException {

        return false;
    }

    public boolean supportsSchemasInProcedureCalls() throws SQLException {

        return false;
    }

    public boolean supportsSchemasInTableDefinitions() throws SQLException {

        return false;
    }

    public boolean supportsSchemasInIndexDefinitions() throws SQLException {

        return false;
    }

    public boolean supportsSchemasInPrivilegeDefinitions() throws SQLException {

        return false;
    }

    public boolean supportsCatalogsInDataManipulation() throws SQLException {

        return false;
    }

    public boolean supportsCatalogsInProcedureCalls() throws SQLException {

        return false;
    }

    public boolean supportsCatalogsInTableDefinitions() throws SQLException {

        return false;
    }

    public boolean supportsCatalogsInIndexDefinitions() throws SQLException {

        return false;
    }

    public boolean supportsCatalogsInPrivilegeDefinitions() throws SQLException {

        return false;
    }

    public boolean supportsPositionedDelete() throws SQLException {

        return false;
    }

    public boolean supportsPositionedUpdate() throws SQLException {

        return false;
    }

    public boolean supportsSelectForUpdate() throws SQLException {

        return false;
    }

    public boolean supportsStoredProcedures() throws SQLException {

        return false;
    }

    public boolean supportsSubqueriesInComparisons() throws SQLException {

        return true;
    }

    public boolean supportsSubqueriesInExists() throws SQLException {

        return true;
    }

    public boolean supportsSubqueriesInIns() throws SQLException {

        return true;
    }

    public boolean supportsSubqueriesInQuantifieds() throws SQLException {

        return true;
    }

    public boolean supportsCorrelatedSubqueries() throws SQLException {

        return true;
    }

    public boolean supportsUnion() throws SQLException {

        return true;
    }

    public boolean supportsUnionAll() throws SQLException {

        return true;
    }

    public boolean supportsOpenCursorsAcrossCommit() throws SQLException {

        return true;
    }

    public boolean supportsOpenCursorsAcrossRollback() throws SQLException {

        return false;
    }

    public boolean supportsOpenStatementsAcrossCommit() throws SQLException {

        return false;
    }

    public boolean supportsOpenStatementsAcrossRollback() throws SQLException {

        return false;
    }

    public int getMaxBinaryLiteralLength() throws SQLException {

        return (SQL_MAX_CHAR_LITERAL_LEN / 8);
    }

    public int getMaxCharLiteralLength() throws SQLException {

        return SQL_MAX_CHAR_LITERAL_LEN;
    }

    public int getMaxColumnNameLength() throws SQLException {

        return 254;
    }

    public int getMaxColumnsInGroupBy() throws SQLException {

        return 0;
    }

    public int getMaxColumnsInIndex() throws SQLException {

        return 0;
    }

    public int getMaxColumnsInOrderBy() throws SQLException {

        return 0;
    }

    public int getMaxColumnsInSelect() throws SQLException {

        return 0;
    }

    public int getMaxColumnsInTable() throws SQLException {

        return 0;
    }

    public int getMaxConnections() throws SQLException {

        return 0;
    }

    public int getMaxCursorNameLength() throws SQLException {

        return 0;
    }

    public int getMaxIndexLength() throws SQLException {

        return 0;
    }

    public int getMaxSchemaNameLength() throws SQLException {

        return 0;
    }

    public int getMaxProcedureNameLength() throws SQLException {

        return 0;
    }

    public int getMaxCatalogNameLength() throws SQLException {

        return 0;
    }

    public int getMaxRowSize() throws SQLException {

        return 0;
    }

    public boolean doesMaxRowSizeIncludeBlobs() throws SQLException {

        return false;
    }

    public int getMaxStatementLength() throws SQLException {

        return 0;
    }

    public int getMaxStatements() throws SQLException {

        return 0;
    }

    public int getMaxTableNameLength() throws SQLException {

        return 254;
    }

    public int getMaxTablesInSelect() throws SQLException {

        return 0;
    }

    public int getMaxUserNameLength() throws SQLException {

        return 31;
    }

    public int getDefaultTransactionIsolation() throws SQLException {

        return Connection.TRANSACTION_READ_COMMITTED;
    }

    public boolean supportsTransactions() throws SQLException {

        return true;
    }

    public boolean supportsTransactionIsolationLevel(int level) throws SQLException {
        switch (level) {
            case Connection.TRANSACTION_READ_COMMITTED:
            case Connection.TRANSACTION_REPEATABLE_READ:
            case Connection.TRANSACTION_SERIALIZABLE:
            case CUBRIDServerSideConnection.TRAN_REP_CLASS_COMMIT_INSTANCE:
                return true;
            default:
                return false;
        }
    }

    public boolean supportsDataDefinitionAndDataManipulationTransactions()
            throws SQLException {
        return true;
    }

    public boolean supportsDataManipulationTransactionsOnly() throws SQLException {
        return true;
    }

    public boolean dataDefinitionCausesTransactionCommit() throws SQLException {
        return false;
    }

    public boolean dataDefinitionIgnoredInTransactions() throws SQLException {
        return false;
    }

    /*
     * empty ResultSet
     */
    public ResultSet getProcedures(
            String catalog, String schemaPattern, String procedureNamePattern) throws SQLException {
        // TODO
        return null;
    }

    /*
     * empty ResultSet
     */
    public ResultSet getProcedureColumns(
            String catalog,
            String schemaPattern,
            String procedureNamePattern,
            String columnNamePattern)
            throws SQLException {
        // TODO
        return null;
    }

    public ResultSet getTables(
            String catalog, String schemaPattern, String tableNamePattern, String[] types)
            throws SQLException {
        // TODO
        return null;
    }
    /*
     * empty ResultSet
     */
    public ResultSet getSchemas() throws SQLException {
        // TODO
        return null;
    }

    /*
     * empty ResultSet
     */
    public ResultSet getCatalogs() throws SQLException {
        // TODO
        return null;
    }

    public ResultSet getTableTypes() throws SQLException {
        // TODO
        return null;
    }

    public ResultSet getColumns(
            String catalog, String schemaPattern, String tableNamePattern, String columnNamePattern)
            throws SQLException {
        // TODO
        return null;
    }

    public ResultSet getColumnPrivileges(
            String catalog, String schema, String table, String columnNamePattern)
            throws SQLException {
        // TODO
        return null;
    }

    public ResultSet getTablePrivileges(
            String catalog, String schemaPattern, String tableNamePattern) throws SQLException {
        // TODO
        return null;
    }

    public ResultSet getBestRowIdentifier(
            String catalog, String schema, String table, int scope, boolean nullable)
            throws SQLException {
        // TODO
        return null;
    }

    /*
     * empty ResultSet
     */
    public ResultSet getVersionColumns(String catalog, String schema, String table)
            throws SQLException {
        // TODO
        return null;
    }

    public ResultSet getPrimaryKeys(String catalog, String schema, String table) {
        // TODO
        return null;
    }

    private ResultSet getForeignKeys(int type, String table1, String table2) throws SQLException {
        // TODO
        return null;
    }

    public ResultSet getImportedKeys(String catalog, String schema, String table)
            throws SQLException {
        // TODO
        return null;
    }

    public ResultSet getExportedKeys(String catalog, String schema, String table)
            throws SQLException {
        // TODO
        return null;
    }

    public ResultSet getCrossReference(
            String primaryCatalog,
            String primarySchema,
            String primaryTable,
            String foreignCatalog,
            String foreignSchema,
            String foreignTable)
            throws SQLException {
        return null;
    }

    public ResultSet getTypeInfo() throws SQLException {
        return null;
    }

    public ResultSet getIndexInfo(
            String catalog, String schema, String table, boolean unique, boolean approximate)
            throws SQLException {
        // TODO
        return null;
    }

    public boolean supportsResultSetType(int type) throws SQLException {
        if (type == ResultSet.TYPE_FORWARD_ONLY) return true;
        if (type == ResultSet.TYPE_SCROLL_INSENSITIVE) return true;
        if (type == ResultSet.TYPE_SCROLL_SENSITIVE) return true;
        return false;
    }

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

    public boolean ownUpdatesAreVisible(int type) throws SQLException {
        if (type == ResultSet.TYPE_SCROLL_SENSITIVE) return true;
        return false;
    }

    public boolean ownDeletesAreVisible(int type) throws SQLException {
        return false;
    }

    public boolean ownInsertsAreVisible(int type) throws SQLException {
        return false;
    }

    public boolean othersUpdatesAreVisible(int type) throws SQLException {
        if (type == ResultSet.TYPE_SCROLL_SENSITIVE) return true;
        return false;
    }

    public boolean othersDeletesAreVisible(int type) throws SQLException {
        return false;
    }

    public boolean othersInsertsAreVisible(int type) throws SQLException {
        return false;
    }

    public boolean updatesAreDetected(int type) throws SQLException {
        return false;
    }

    public boolean deletesAreDetected(int type) throws SQLException {
        if (type == ResultSet.TYPE_SCROLL_SENSITIVE) return true;
        return false;
    }

    public boolean insertsAreDetected(int type) throws SQLException {
        return false;
    }

    public boolean supportsBatchUpdates() throws SQLException {
        return true;
    }

    public ResultSet getUDTs(
            String catalog, String schemaPattern, String typeNamePattern, int[] types)
            throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Connection getConnection() throws SQLException {
        return con;
    }

    // 3.0
    public ResultSet getAttributes(
            String catalog,
            String schemaPattern,
            String typeNamePattern,
            String attributeNamePattern)
            throws SQLException {
        // TODO
        return null;
    }

    public int getDatabaseMajorVersion() throws SQLException {
        if (this.major_version == -1) {
            getDatabaseProductVersion();
        }
        return this.major_version;
    }

    public int getDatabaseMinorVersion() throws SQLException {

        if (this.minor_version == -1) {
            getDatabaseProductVersion();
        }
        return this.minor_version;
    }

    public int getJDBCMajorVersion() throws SQLException {
        return 3;
    }

    public int getJDBCMinorVersion() throws SQLException {

        return 0;
    }

    public int getResultSetHoldability() throws SQLException {
        return con.getHoldability();
    }

    public int getSQLStateType() throws SQLException {
        return DatabaseMetaData.sqlStateSQL99;
    }

    public boolean locatorsUpdateCopy() throws SQLException {
        return false;
    }

    public boolean supportsGetGeneratedKeys() throws SQLException {
        return false;
    }

    public boolean supportsMultipleOpenResults() throws SQLException {
        return true;
    }

    public boolean supportsNamedParameters() throws SQLException {
        return false;
    }

    public boolean supportsResultSetHoldability(int holdability) throws SQLException {
        if (holdability == ResultSet.CLOSE_CURSORS_AT_COMMIT
                || holdability == ResultSet.HOLD_CURSORS_OVER_COMMIT) return true;
        return false;
    }

    public boolean supportsSavepoints() throws SQLException {
        return true;
    }

    public boolean supportsStatementPooling() throws SQLException {
        return false;
    }

    public ResultSet getSuperTables(
            String catalog, String schemaPattern, String tableNamePattern) throws SQLException {
        // TODO
        return null;
    }

    public ResultSet getSuperTypes(
            String catalog, String schemaPattern, String typeNamePattern) throws SQLException {
        // TODO
        return null;
    }

    // 3.0

    void close() {
        // TODO
        isClosed = true;
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
    public boolean autoCommitFailureClosesAllResultSets() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public ResultSet getClientInfoProperties() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public ResultSet getFunctionColumns(
            String catalog,
            String schemaPattern,
            String functionNamePattern,
            String columnNamePattern)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public ResultSet getFunctions(String catalog, String schemaPattern, String functionNamePattern)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public RowIdLifetime getRowIdLifetime() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public ResultSet getSchemas(String catalog, String schemaPattern) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
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
    public ResultSet getPseudoColumns(
            String catalog, String schemaPattern, String tableNamePattern, String columnNamePattern)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public boolean generatedKeyAlwaysReturned() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }
}
