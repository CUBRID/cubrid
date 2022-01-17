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

public class TestChangedSpecTest {

    /* Multiple SQL with Statement */
    public static String test1() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a int auto_increment", "b int");

        String result = "f";
        Statement stmt = conn.createStatement();
        try {
            stmt.execute("select * from t1; insert into t1 (b) values (1); select * from t1");
        } catch (Exception e) {
            result = "t";
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return result;
    }

    /* Multiple SQL with PreparedStatement */
    public static String test2() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a int auto_increment", "b int");

        String result = "f";
        try {
            String query = "select * from t1; insert into t1 (b) values (1); select * from t1";
            PreparedStatement stmt = conn.prepareStatement(query);
            stmt.getMoreResults();
        } catch (Exception e) {
            result = "t";
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return result;
    }
}
