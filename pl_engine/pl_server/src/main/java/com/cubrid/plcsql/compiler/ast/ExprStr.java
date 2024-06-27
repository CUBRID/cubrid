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

import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import org.antlr.v4.runtime.ParserRuleContext;
import org.apache.commons.text.StringEscapeUtils;

public class ExprStr extends Expr {

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitExprStr(this);
    }

    public final String val;

    public ExprStr(ParserRuleContext ctx, String val) {
        super(ctx);

        this.val = val;
    }

    public String javaCode() {

        int len = val.length();
        if (len <= STR_LITERAL_CUT_LEN) {
            return "(\"" + StringEscapeUtils.escapeJava(val) + "\")";
        } else {
            StringBuilder sbuilder = new StringBuilder();
            boolean first = true;
            sbuilder.append("String.join(\"\", new String[] {");

            for (int b = 0; b < len; b += STR_LITERAL_CUT_LEN) {
                if (first) {
                    first = false;
                } else {
                    sbuilder.append(", ");
                }
                int e = Math.min(len, b + STR_LITERAL_CUT_LEN);
                sbuilder.append('"' + StringEscapeUtils.escapeJava(val.substring(b, e)) + '"');
            }
            sbuilder.append("})");
            return sbuilder.toString();
        }
    }

    // -------------------------------------------------------------------
    // Private
    // -------------------------------------------------------------------

    // 16383 * 4 = 65532 <= 65534 where
    //   65534: maximum byte length of a Java string literal,
    //   4: maximum byte length of a character in UTF-8
    private static final int STR_LITERAL_CUT_LEN = 16383;
}
