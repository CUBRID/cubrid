/*
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

package com.cubrid.plcsql.compiler;

import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.data.ColumnInfo;
import java.util.ArrayList;
import java.util.List;

public class SqlSemantics {

    public int seqNo;

    // for error return
    public int errCode; // non-zero if error
    public String errMsg;

    public SqlSemantics(int seqNo, int errCode, String errMsg) {
        assert errCode != 0;
        this.seqNo = seqNo;
        this.errCode = errCode;
        this.errMsg = errMsg;
    }

    // for normal return
    public int kind;
    public String rewritten;
    public List<PlParamInfo>
            hostVars; // host variables and their SQL types required in their locations
    public List<ColumnInfo> selectList; // (only for select statements) columns and their SQL types
    public List<String> intoVars; // (only for select stetements with an into-clause) into variables

    public List<ColumnInfo> columnInfos;

    public SqlSemantics(
            int seqNo,
            int kind,
            String rewritten,
            List<PlParamInfo> hostVars,
            List<ColumnInfo> selectList,
            List<String> intoVars) {

        this.seqNo = seqNo;
        this.kind = kind;
        this.rewritten = rewritten;
        this.hostVars = hostVars;
        this.selectList = selectList;
        this.intoVars = intoVars;
    }

    public SqlSemantics(CUBRIDUnpacker unpacker) {
        this.seqNo = unpacker.unpackInt();
        this.kind = unpacker.unpackInt();
        this.rewritten = unpacker.unpackCString();

        if (this.kind < 0) {
            this.errCode = this.kind;
            this.errMsg = this.rewritten;
        }

        int selectListCnt = unpacker.unpackInt();
        if (selectListCnt > 0) {
            selectList = new ArrayList<>();
            for (int i = 0; i < selectListCnt; i++) {
                selectList.add(new ColumnInfo(unpacker));
            }
        }

        // TODO
        int hostVarsCnt = unpacker.unpackInt();
        if (hostVarsCnt > 0) {
            hostVars = new ArrayList<>();
            for (int i = 0; i < hostVarsCnt; i++) {
                hostVars.add(new PlParamInfo(unpacker));
            }
        }
        /*
        for (int i = 0; i < hostVarsCnt; i++) {
            String var = unpacker.unpackCString();
            String type = unpacker.unpackCString();
            hostVars.add(var, type);
        }
        */

        // TODO
        int intoVarsCnt = unpacker.unpackInt();
        if (intoVarsCnt > 0) {
            intoVars = new ArrayList<>();
        }
        /*
        for (int i = 0; i < intoVarsCnt; i++) {
            String var = unpacker.unpackCString();
            intoVars.add(var);
        }
        */
    }
}
