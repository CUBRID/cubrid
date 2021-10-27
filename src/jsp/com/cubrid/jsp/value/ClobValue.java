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

package com.cubrid.jsp.value;

import com.cubrid.jsp.impl.SUConnection;
import com.cubrid.jsp.Server;
import com.cubrid.jsp.data.LobHandleInfo;
import com.cubrid.jsp.data.SOID;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.jdbc.CUBRIDServerSideClob;
import com.cubrid.jsp.jdbc.CUBRIDServerSideConnection;

import java.sql.Clob;
import java.sql.DriverManager;
import java.sql.SQLException;

public class ClobValue extends Value {
    private LobHandleInfo lobInfo = null;
    private Clob value = null;

    public ClobValue(Clob value) {
        super();
        this.value = value;
    }

    public ClobValue(Clob value, int mode, int dbType) {
        super(mode);
        this.value = value;
        this.dbType = dbType;
    }

    public Clob toClob(SUConnection conn, String charset) throws TypeMismatchException {
        if (value == null) {
            try {
                value = new CUBRIDServerSideClob(conn, lobInfo, charset, true);
            } catch (SQLException e) {
            }
        }
        return value;
    }

    public String toString() {
        return value.toString();
    }

    public String[] toStringArray() throws TypeMismatchException {
        return new String[] { toString() };
    }

    public Object toObject() throws TypeMismatchException {
        // TODO
        // return toClob();
        return null;
    }

    public Object[] toObjectArray() throws TypeMismatchException {
        // TODO
        // return new Object[] { toClob() };
        return null;
    }
}
