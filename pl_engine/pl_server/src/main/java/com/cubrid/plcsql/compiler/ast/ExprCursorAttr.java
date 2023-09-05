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

public class ExprCursorAttr extends Expr {

    public enum Attr {
        ISOPEN(TypeSpecSimple.BOOLEAN, "isOpen"),
        FOUND(TypeSpecSimple.BOOLEAN, "found"),
        NOTFOUND(TypeSpecSimple.BOOLEAN, "notFound"),
        ROWCOUNT(TypeSpecSimple.BIGINT, "rowCount");

        Attr(TypeSpecSimple ty, String method) {
            this.ty = ty;
            this.method = method;
        }

        public final TypeSpecSimple ty;
        private final String method;
    }

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitExprCursorAttr(this);
    }

    public final ExprId id;
    public final Attr attr;

    public ExprCursorAttr(ParserRuleContext ctx, ExprId id, Attr attr) {
        super(ctx);

        this.id = id;
        this.attr = attr;
    }

    @Override
    public String exprToJavaCode() {
        if (attr == Attr.ISOPEN) {
            return tmplIsOpen
                    .replace("%'CURSOR'%", id.toJavaCode())
                    .replace("%'METHOD'%", attr.method);
        } else {
            return tmplOthers
                    .replace("%'CURSOR'%", id.toJavaCode())
                    .replace("%'JAVA-TYPE'%", attr.ty.toJavaCode())
                    .replace(
                            "%'SUBMSG'%",
                            "tried to retrieve an attribute from an unopened SYS_REFCURSOR")
                    .replace("%'METHOD'%", attr.method);
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplIsOpen =
            "((%'CURSOR'% == null) ? Boolean.FALSE : %'CURSOR'%.%'METHOD'%())";

    private static final String tmplOthers =
            "((%'CURSOR'% == null) ? (%'JAVA-TYPE'%) throwInvalidCursor(\"%'SUBMSG'%\") : %'CURSOR'%.%'METHOD'%())";
}
