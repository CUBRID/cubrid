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

package com.cubrid.plcsql.builtin;

import com.cubrid.jsp.SysParam;
import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.context.ContextManager;
import com.cubrid.jsp.jdbc.CUBRIDServerSideConnection;
import java.util.ArrayList;
import java.util.Arrays;

public class DBMS_OUTPUT {

    public DBMS_OUTPUT() {}

    private static Context getContext() {
        return ContextManager.getContextofCurrentThread();
    }

    public static void enable(int size) throws Exception {
        Context c = getContext();
        c.getMessageBuffer().enable(size);

        CUBRIDServerSideConnection conn = (CUBRIDServerSideConnection) c.getConnection();
        conn.getSUConnection()
                .setPLSessionParams(
                        new ArrayList<SysParam>(
                                Arrays.asList(
                                        new SysParam(
                                                SysParam.DBMS_OUTPUT,
                                                SysParam.PRM_TYPE_BOOLEAN,
                                                true))));
    }

    public static void disable() throws Exception {
        Context c = getContext();
        c.getMessageBuffer().disable();

        CUBRIDServerSideConnection conn = (CUBRIDServerSideConnection) c.getConnection();
        conn.getSUConnection()
                .setPLSessionParams(
                        new ArrayList<SysParam>(
                                Arrays.asList(
                                        new SysParam(
                                                SysParam.DBMS_OUTPUT,
                                                SysParam.PRM_TYPE_BOOLEAN,
                                                false))));
    }

    public static void getLine(String[] line, int[] status) {
        Context c = getContext();
        if (Context.getSystemParam(SysParam.DBMS_OUTPUT) != null
                && Context.getSystemParameterBool(SysParam.DBMS_OUTPUT)) {
            c.getMessageBuffer().enable();
        }
        String str = c.getMessageBuffer().getLine();
        line[0] = str;
        status[0] = c.getMessageBuffer().getStatus();
    }

    public static void getLines(String[] line, int[] cnt) {
        Context c = getContext();
        if (Context.getSystemParam(SysParam.DBMS_OUTPUT) != null
                && Context.getSystemParameterBool(SysParam.DBMS_OUTPUT)) {
            c.getMessageBuffer().enable();
        }

        String[] strs = c.getMessageBuffer().getLines(cnt[0]);

        StringBuilder builder = new StringBuilder();

        if (strs != null) {
            for (int i = 0; i < strs.length; i++) {
                builder.append(strs[i]);
                if (i != strs.length - 1) {
                    builder.append(System.lineSeparator());
                }
            }
        }

        cnt[0] = strs.length;
        line[0] = builder.toString();
    }

    public static void putLine(String line) {
        Context c = getContext();
        if (Context.getSystemParam(SysParam.DBMS_OUTPUT) != null
                && Context.getSystemParameterBool(SysParam.DBMS_OUTPUT)) {
            c.getMessageBuffer().enable();
        }
        c.getMessageBuffer().putLine(line);
    }

    public static void put(String str) {
        Context c = getContext();
        if (Context.getSystemParam(SysParam.DBMS_OUTPUT) != null
                && Context.getSystemParameterBool(SysParam.DBMS_OUTPUT)) {
            c.getMessageBuffer().enable();
        }
        c.getMessageBuffer().put(str);
    }

    public static void newLine() {
        Context c = getContext();
        if (Context.getSystemParam(SysParam.DBMS_OUTPUT) != null
                && Context.getSystemParameterBool(SysParam.DBMS_OUTPUT)) {
            c.getMessageBuffer().enable();
        }
        c.getMessageBuffer().newLine();
    }
}
