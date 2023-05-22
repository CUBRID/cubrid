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
package com.cubrid.jsp.impl;

import com.cubrid.jsp.ExecuteThread;
import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.jdbc.CUBRIDServerSideJDBCErrorCode;
import com.cubrid.jsp.jdbc.CUBRIDServerSideJDBCErrorManager;
import java.io.UnsupportedEncodingException;
import java.sql.SQLException;

public class SUBindParameter extends SUParameter {
    private static final byte PARAM_MODE_UNKNOWN = 0;
    private static final byte PARAM_MODE_IN = 1;
    private static final byte PARAM_MODE_OUT = 2;

    private boolean isBinded[];
    private byte paramMode[];

    SUBindParameter(int pNumber) {
        super(pNumber);

        isBinded = new boolean[pNumber];
        paramMode = new byte[pNumber];

        clear();
    }

    void clear() {
        for (int i = 0; i < number; i++) {
            isBinded[i] = false;
            paramMode[i] = PARAM_MODE_UNKNOWN;
            values[i] = null;
            types[i] = DBType.DB_NULL;
        }
    }

    boolean checkAllBinded() {
        for (int i = 0; i < number; i++) {
            if (isBinded[i] == false && paramMode[i] == PARAM_MODE_UNKNOWN) return false;
        }
        return true;
    }

    void close() {
        for (int i = 0; i < number; i++) {
            values[i] = null;
        }

        isBinded = null;
        paramMode = null;
        values = null;
        types = null;
    }

    public void setParameter(int index, int bType, Object bValue) throws SQLException {
        if (index < 0 || index >= number) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_INVALID_ARGUMENT, null);
        }

        types[index] = bType;
        values[index] = bValue;

        isBinded[index] = true;
        paramMode[index] |= PARAM_MODE_IN;
    }

    public void setOutParam(int index, int sqlType) throws SQLException {
        if (index < 0 || index >= number) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_INVALID_ARGUMENT, null);
        }

        paramMode[index] |= PARAM_MODE_OUT;
    }

    synchronized void pack(CUBRIDPacker packer) throws UnsupportedEncodingException {
        int cnt = paramMode.length;
        packer.packInt(cnt);
        for (int i = 0; i < cnt; i++) {
            packer.packValue(values[i], types[i], ExecuteThread.charSet);
            packer.packInt((int) paramMode[i]);
        }
    }
}
