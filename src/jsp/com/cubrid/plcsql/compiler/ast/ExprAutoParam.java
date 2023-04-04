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

import com.cubrid.jsp.value.Value;
import com.cubrid.jsp.value.ValueUtilities;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.data.DBType;
import com.cubrid.plcsql.compiler.ast.Expr;
import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import org.antlr.v4.runtime.ParserRuleContext;
import org.apache.commons.text.StringEscapeUtils;

import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;

public class ExprAutoParam extends Expr {

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitExprAutoParam(this);
    }

    public final Value val;

    public ExprAutoParam(ParserRuleContext ctx, Value val) {
        super(ctx);

        this.val = val;
    }

    @Override
    public String exprToJavaCode() {

        int type = val.getDbType();
        if (type == DBType.DB_NULL) {
            return "null";
        }

        Object javaObj;
        try {
            javaObj = ValueUtilities.resolveValue(type, val);
        } catch (TypeMismatchException e) {
            throw new RuntimeException("Internal Error", e);
        }
        if (javaObj == null) {
            return "null";
        }

        switch (type) {
            case DBType.DB_CHAR:
            case DBType.DB_STRING:
                assert javaObj instanceof String;
                return String.format("\"%s\"", StringEscapeUtils.escapeJava((String) javaObj));
            case DBType.DB_SHORT:
                assert javaObj instanceof Short;
                return String.format("new Short((short) %s)", javaObj);
            case DBType.DB_INT:
                assert javaObj instanceof Integer;
                return String.format("new Integer(%s)", javaObj);
            case DBType.DB_BIGINT:
                assert javaObj instanceof Long;
                return String.format("new Long(%sL)", javaObj);
            case DBType.DB_NUMERIC:
                assert javaObj instanceof BigDecimal;
                return String.format("new BigDecimal(\"%s\")", javaObj);
            case DBType.DB_FLOAT:
                assert javaObj instanceof Float;
                return String.format("new Float(%sF)", javaObj);
            case DBType.DB_DOUBLE:
                assert javaObj instanceof Double;
                return String.format("new Double(%s)", javaObj);
            case DBType.DB_DATE:
                assert javaObj instanceof Date;
                return String.format("Date.valueOf(\"%s\")", javaObj);
            case DBType.DB_TIME:
                assert javaObj instanceof Time;
                return String.format("Time.valueOf(\"%s\")", javaObj);
            case DBType.DB_DATETIME:
            case DBType.DB_TIMESTAMP:
                assert javaObj instanceof Timestamp;
                return String.format("Timestamp.valueOf(\"%s\")", javaObj);
            default:
                assert false: "unreachable";
                throw new RuntimeException("unreachable");
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
