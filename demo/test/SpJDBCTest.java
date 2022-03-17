package test;

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

import java.sql.*;
import java.util.Enumeration;
import java.util.Properties;

/*
 * [@demodb]
 * java_stored_procedure=yes
 * java_stored_procedure_port=9999
 */

public class SpJDBCTest {

    /*
     * CREATE OR REPLACE FUNCTION testDBMetadataSP() RETURN STRING AS LANGUAGE JAVA NAME 'SpJDBCTest.testDatabaseMetadata() return java.lang.String';
     */
    public static String testDatabaseMetadata() {
        String result = "";
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

            DatabaseMetaData meta = conn.getMetaData();

            result += "getURL(): " + meta.getURL() + "\n";
            result += "getUserName(): " + meta.getUserName() + "\n";

            // TODO

        } catch (Exception e) {
            result = e.getMessage();
        }
        return result;
    }

    /*
     * CREATE OR REPLACE FUNCTION testConnection() RETURN STRING AS LANGUAGE JAVA NAME 'SpJDBCTest.testConnection() return java.lang.String';
     */
    public static String testConnection() {
        String result = "";
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:");

            result += "getTransactionIsolation(): " + conn.getTransactionIsolation() + "\n";

            conn.close();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return e.getMessage();
        }
    }

    /*
     * CREATE OR REPLACE FUNCTION testStatementExecute(query string) RETURN STRING AS LANGUAGE JAVA NAME 'SpJDBCTest.testStatementExecute(java.lang.String) return java.lang.String';
     */
    public static String testStatementExecute(String query) {
        String result = "";
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            Statement stmt = conn.createStatement();
            boolean rs = stmt.execute(query);
            if (rs) {
                result = "true";
            } else {
                result = "false";
            }

            stmt.close();
            conn.close();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return e.getMessage();
        }
    }

    /* test_jdbc/src/cubrid/jdbc/common/usage/TestBlob.java */
    // CREATE OR REPLACE FUNCTION testBlob() RETURN STRING AS LANGUAGE JAVA NAME
    // 'SpJDBCTest.testBlob() return java.lang.String';
    public static String testBlob() {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            Statement stmt = conn.createStatement();
            ResultSet rs = null;

            byte[] blobData = new byte[32];
            for (int i = 0; i < blobData.length; i++) {
                blobData[i] = (byte) i;
            }

            stmt.executeUpdate("DROP TABLE IF EXISTS testBlob1");
            stmt.executeUpdate("CREATE TABLE testBlob1(blobField BLOB)");

            PreparedStatement pStmt =
                    conn.prepareStatement("INSERT INTO testBlob1(blobField) VALUES (?)");
            Blob blobdatas = conn.createBlob();
            blobdatas.setBytes(1, blobData);
            pStmt.setBlob(1, blobdatas);
            pStmt.executeUpdate();

            rs = stmt.executeQuery("SELECT blobField FROM testBlob1");
            rs.next();

            Blob blob = rs.getBlob(1);

            byte[] newBlobData = blob.getBytes(1L, (int) blob.length());

            stmt.close();
            pStmt.close();
            conn.close();
            return new String(newBlobData);
        } catch (Exception e) {
            e.printStackTrace();
            return e.getMessage();
        }
    }

    public static String testUserInfo() {
        String result = "";
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:");

            Properties props = conn.getClientInfo();

            Enumeration keys = props.keys();
            while (keys.hasMoreElements()) {
                String key = (String) keys.nextElement();
                String value = (String) props.get(key);
                result += "(" + key + ": " + value + ") \n";
            }

            conn.close();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return e.getMessage();
        }
    }
}
