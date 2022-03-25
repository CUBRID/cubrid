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

package test;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;

public class SpErrorTest {

    public static String testSQLExceptionThrownTest(String a) throws SQLException {
        throw new SQLException(a);
    }

    public static String testSQLExceptionTest(String a) {
        try {
            throw new SQLException(a);
        } catch (Exception e) {
            return "ok";
        }
    }

    public static int testDivideByZeroTest(int a) {
        int c = a / 0;
        return c;
    }

    public static int testFiboError(int n) throws ClassNotFoundException, SQLException {
        if (n == 0) {
            return 0;
        } else if (n == 1) {
            return 1;
        } else {
            int result = 0;
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            String fiboStmt = "SELECT testFiboError (?) + testFiboError (?);";
            PreparedStatement stmt = conn.prepareStatement(fiboStmt);
            stmt.setInt(1, n - 1);
            stmt.setInt(2, n - 2);
            ResultSet rs = stmt.executeQuery();
            rs.next();
            result += rs.getInt(1);
            return result;
        }
    }

    public static int testInvalidQueryTest() throws SQLException {
        String invalidQuery = "SELECT * from ";

        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement stmt = conn.createStatement();
        stmt.executeQuery(invalidQuery);

        return 0;
    }

    public static int testUnsupportedTest() throws SQLException {
        String query = "SELECT * from game;";

        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement stmt = conn.createStatement();
        stmt.addBatch(query); // throws SQLException wrapping UnSupportedException

        return 0;
    }
}
