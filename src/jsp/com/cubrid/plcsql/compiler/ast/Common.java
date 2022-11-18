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

package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

class Common {

    static String getSetUsedValuesStr(NodeList<? extends Expr> exprList) {
        return getSetUsedValuesStr(exprList, 1);
    }

    static String getSetUsedValuesStr(NodeList<? extends Expr> exprList, int startIndex) {

        if (exprList == null || exprList.nodes.size() == 0) {
            return "// no used values";
        }

        StringBuffer sbuf = new StringBuffer();
        int size = exprList.nodes.size();
        for (int i = 0; i < size; i++) {
            if (i > 0) {
                sbuf.append("\n");
            }
            Expr expr = exprList.nodes.get(i);
            sbuf.append(
                    tmplSetObject
                            .replace("%'INDEX'%", "" + (i + startIndex))
                            .replace("  %'VALUE'%", Misc.indentLines(expr.toJavaCode(), 1)));
        }

        return sbuf.toString();
    }

    static String getQuestionMarks(int n) {
        StringBuffer sbuf = new StringBuffer();
        boolean first = true;
        for (int i = 0; i < n; i++) {

            if (first) {
                first = false;
            } else {
                sbuf.append(", ");
            }

            sbuf.append("?");
        }

        return sbuf.toString();
    }

    // ----------------------------------------------------
    // Private
    // ----------------------------------------------------

    private static final String tmplSetObject =
            Misc.combineLines("stmt_%'LEVEL'%.setObject(", "  %'INDEX'%,", "  %'VALUE'%", ");");
}
