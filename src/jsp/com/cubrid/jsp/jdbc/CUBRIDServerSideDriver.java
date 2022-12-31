/*
 *
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

import java.lang.reflect.Method;
import java.sql.Connection;
import java.sql.Driver;
import java.sql.DriverManager;
import java.sql.DriverPropertyInfo;
import java.sql.SQLException;
import java.util.Properties;
import java.util.StringTokenizer;
import java.util.logging.Logger;

public class CUBRIDServerSideDriver implements Driver {

    private static final String JDBC_DEFAULT_CONNECTION = "jdbc:default:connection";

    private static String VERSION_STRING;
    private static int VERSION_MAJOR;
    private static int VERSION_MINOR;
    private static int VERSION_PATCH;

    static {
        try {
            DriverManager.registerDriver(new CUBRIDServerSideDriver());
        } catch (SQLException e) {
        }

        try {
            // assume that this class is loaded after "cubrid.server.version" is set
            VERSION_STRING = System.getProperty("cubrid.server.version");

            StringTokenizer st = new StringTokenizer(VERSION_STRING, ".");
            if (st.countTokens() != 4) {
                throw new RuntimeException("Could not parse version_string: " + VERSION_STRING);
            }
            VERSION_MAJOR = Integer.parseInt(st.nextToken());
            VERSION_MINOR = Integer.parseInt(st.nextToken());
            VERSION_PATCH = Integer.parseInt(st.nextToken());

        } catch (Exception e) {
        }
    }

    @Override
    public Connection connect(String url, Properties info) throws SQLException {
        if (!acceptsURL(url)) {
            return null;
        }

        Connection conn = null;
        try {
            Thread t = Thread.currentThread();
            conn =
                    (Connection)
                            invoke(
                                    "com.cubrid.jsp.ExecuteThread",
                                    "createConnection",
                                    null,
                                    t,
                                    null);
        } catch (Exception e) {
            /* do nothing. The exception will be dealt with in ExecuteThread */
        }

        return conn;
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
    public DriverPropertyInfo[] getPropertyInfo(String url, Properties info) throws SQLException {
        return new DriverPropertyInfo[0];
    }

    @Override
    public int getMajorVersion() {
        return VERSION_MAJOR;
    }

    @Override
    public int getMinorVersion() {
        return VERSION_MINOR;
    }

    @Override
    public boolean jdbcCompliant() {
        return true;
    }

    @Override
    public Logger getParentLogger() {
        throw new java.lang.UnsupportedOperationException();
    }

    private Object invoke(
            String cls_name, String method, Class<?>[] param_cls, Object cls, Object[] params) {
        try {
            Class<?> c = Class.forName(cls_name);
            Method m = c.getMethod(method, param_cls);
            return m.invoke(cls, params);
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
