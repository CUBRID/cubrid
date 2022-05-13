package com.cubrid.jsp.jdbc;

import java.sql.Connection;
import java.sql.Driver;
import java.sql.DriverManager;
import java.sql.DriverPropertyInfo;
import java.sql.SQLException;
import java.sql.SQLFeatureNotSupportedException;
import java.util.Properties;
import java.util.logging.Logger;

import com.cubrid.jsp.ExecuteThread;

public class CUBRIDServerSideDriver implements Driver {

    private static final String JDBC_DEFAULT_CONNECTION = "jdbc:default:connection";

    static {
        try {
            DriverManager.registerDriver(new CUBRIDServerSideDriver());
        } catch (SQLException e) {
        }
    }

    @Override
    public boolean acceptsURL(String url) throws SQLException {
        if (url == null) {
            return false;
        }

        if (url.toLowerCase().startsWith(JDBC_DEFAULT_CONNECTION)) {
            return true;
        }

        return false;
    }

    @Override
    public Connection connect(String url, Properties info) throws SQLException {
        if (!acceptsURL(url)) {
            return null;
        }
        if (url.toLowerCase().startsWith(JDBC_DEFAULT_CONNECTION)) {
            return defaultConnection();
        }
        return null;
    }

    public Connection defaultConnection() {
        return new CUBRIDServerSideConnection((ExecuteThread) Thread.currentThread());
    }

    @Override
    public int getMajorVersion() {
        return 11;
    }

    @Override
    public int getMinorVersion() {
        return 2;
    }

    @Override
    public Logger getParentLogger() throws SQLFeatureNotSupportedException {
        return null;
    }

    @Override
    public DriverPropertyInfo[] getPropertyInfo(String url, Properties info) throws SQLException {
        return new DriverPropertyInfo[0];
    }

    @Override
    public boolean jdbcCompliant() {
        return true;
    }
}
