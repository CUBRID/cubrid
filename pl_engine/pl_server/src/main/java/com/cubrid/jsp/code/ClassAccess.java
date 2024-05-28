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
package com.cubrid.jsp.code;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.Base64;

public class ClassAccess {
    public static String getCodeMeta(Connection conn, String name) throws SQLException {
        String ts = null;
        String sql =
                "SELECT created_time, is_static, is_system_generated FROM _db_stored_procedure_code WHERE name = ?";
        PreparedStatement prepStmt = conn.prepareStatement(sql);
        prepStmt.setString(1, name);

        ResultSet rs = prepStmt.executeQuery();
        if (rs.next()) {
            ts = rs.getString(1);
        }
        rs.close();
        return ts;
    }

    public static String getTransactionKey(Connection conn, String name) throws SQLException {
        String ts = null;
        String sql = "SELECT created_time FROM _db_stored_procedure_code WHERE name = ?";
        PreparedStatement prepStmt = conn.prepareStatement(sql);
        prepStmt.setString(1, name);

        ResultSet rs = prepStmt.executeQuery();
        if (rs.next()) {
            ts = rs.getString(1);
        }
        rs.close();
        return ts;
    }

    public static byte[] getObjectCodeBytes(Connection conn, String name) throws SQLException {
        byte[] jar = null;

        String sql = "SELECT ocode FROM _db_stored_procedure_code WHERE name = ?";
        PreparedStatement prepStmt = conn.prepareStatement(sql);
        prepStmt.setString(1, name);

        ResultSet rs = prepStmt.executeQuery();
        if (rs.next()) {
            String str = rs.getString(1);
            jar = Base64.getDecoder().decode(str);
        }
        rs.close();
        return jar;
    }

    public static CompiledCodeSet getObjectCode(Connection conn, Signature sig) throws Exception {
        CompiledCodeSet code = null;
        String className = sig.getClassName();

        String tKey = null;
        try {
            tKey = getTransactionKey(conn, className);
        } catch (SQLException e) {
        }

        if (tKey == null) {
            return null;
        }

        byte[] jarCode = ClassAccess.getObjectCodeBytes(conn, className);
        if (jarCode != null) {
            code = CompiledCodeSet.loadFromJar(className, jarCode);
            code.setTimestamp(tKey);
        }

        return code;
    }
}
