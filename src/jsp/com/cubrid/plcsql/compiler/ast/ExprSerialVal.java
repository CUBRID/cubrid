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
import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import org.antlr.v4.runtime.ParserRuleContext;

public class ExprSerialVal extends Expr {

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitExprSerialVal(this);
    }

    public enum SerialVal {
        CURR_VAL,
        NEXT_VAL,
    }

    public final String name;
    public final SerialVal mode; // CURR_VAL or NEXT_VAL

    public boolean verified;

    public ExprSerialVal(ParserRuleContext ctx, String name, SerialVal mode) {
        super(ctx);

        this.name = name;
        this.mode = mode;
    }

    @Override
    public String exprToJavaCode() {

        return tmplSerialVal
                .replace("%'SERIAL-NAME'%", name)
                .replace(
                        "%'SERIAL-VAL'%",
                        (mode == SerialVal.CURR_VAL) ? "CURRENT_VALUE" : "NEXT_VALUE");
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplSerialVal =
            Misc.combineLines(
                    "(new Object() {",
                    "  int getSerialVal() throws Exception {",
                    "    int ret;",
                    "    String dynSql = \"select %'SERIAL-NAME'%.%'SERIAL-VAL'%\";",
                    "    PreparedStatement stmt = conn.prepareStatement(dynSql);",
                    "    ResultSet r = stmt.executeQuery();",
                    "    if (r.next()) {",
                    "      ret = r.getInt(1);",
                    "    } else {",
                    "      assert false; // serial value must be present",
                    "      ret = -1;",
                    "    }",
                    "    stmt.close();",
                    "    return ret;",
                    "  }",
                    "}.getSerialVal())");
}
