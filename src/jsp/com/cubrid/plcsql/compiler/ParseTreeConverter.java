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

import static com.cubrid.plcsql.compiler.antlrgen.PcsParser.*;

import com.cubrid.plcsql.compiler.antlrgen.PcsParserBaseVisitor;
import com.cubrid.plcsql.compiler.ast.*;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.LocalTime;
import java.time.ZonedDateTime;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;
import org.antlr.v4.runtime.ParserRuleContext;
import org.antlr.v4.runtime.tree.ParseTreeWalker;
import org.apache.commons.text.StringEscapeUtils;

// parse tree --> AST converter
public class ParseTreeConverter extends PcsParserBaseVisitor<AstNode> {

    public final SymbolStack symbolStack = new SymbolStack();

    @Override
    public AstNode visitSql_script(Sql_scriptContext ctx) {

        AstNode ret = visitCreate_routine(ctx.create_routine());

        assert symbolStack.getSize() == 2;
        assert EMPTY_PARAMS.nodes.size() == 0;
        assert EMPTY_ARGS.nodes.size() == 0;

        return ret;
    }

    @Override
    public Unit visitCreate_routine(Create_routineContext ctx) {

        previsitRoutine_definition(ctx.routine_definition());
        DeclRoutine decl = visitRoutine_definition(ctx.routine_definition());
        return new Unit(ctx,
                autonomousTransaction,
                connectionRequired,
                getImportString(),
                decl);
    }

    @Override
    public NodeList<DeclParam> visitParameter_list(Parameter_listContext ctx) {

        if (ctx == null) {
            return EMPTY_PARAMS;
        }

        NodeList<DeclParam> ret = new NodeList<>();
        for (ParameterContext pc : ctx.parameter()) {
            ret.addNode((DeclParam) visit(pc));
        }

        return ret;
    }

    @Override
    public DeclParamIn visitParameter_in(Parameter_inContext ctx) {
        String name = Misc.getNormalizedText(ctx.parameter_name());
        TypeSpec typeSpec = (TypeSpec) visit(ctx.type_spec());

        DeclParamIn ret = new DeclParamIn(ctx, name, typeSpec);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    @Override
    public DeclParamOut visitParameter_out(Parameter_outContext ctx) {
        String name = Misc.getNormalizedText(ctx.parameter_name());
        TypeSpec typeSpec = (TypeSpec) visit(ctx.type_spec());

        DeclParamOut ret = new DeclParamOut(ctx, name, typeSpec);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    @Override
    public TypeSpec visitPercent_type_spec(Percent_type_specContext ctx) {

        if (ctx.table_name() == null) {
            // case variable%TYPE
            ExprId id = visitNonFuncIdentifier(ctx.identifier());
            if (id == null) {
                throw new SemanticError(Misc.getLineOf(ctx),    // s000
                    "undeclared id " + Misc.getNormalizedText(ctx.identifier()));
            }
            if (!(id.decl instanceof DeclVarLike)) {
                throw new SemanticError(Misc.getLineOf(ctx),    // s001
                    Misc.getNormalizedText(ctx.identifier()) +
                    " must be a procedure/function parameter, variable, or constant");
            }

            return ((DeclVarLike) id.decl).typeSpec();
        } else {
            // case table.column%TYPE
            String table = Misc.getNormalizedText(ctx.table_name());
            String column = Misc.getNormalizedText(ctx.identifier());

            return new TypeSpecPercent(table, column);
        }

    }

    @Override
    public TypeSpecSimple visitNumeric_type(Numeric_typeContext ctx) {
        int precision = -1, scale = -1;
        try {
            if (ctx.precision != null) {
                precision = Integer.parseInt(ctx.precision.getText());
                if (ctx.scale != null) {
                    scale = Integer.parseInt(ctx.scale.getText());
                }
            }
        } catch (NumberFormatException e) {
            assert false;   // by syntax
            throw new RuntimeException("unreachable");
        }

        // TODO: restore two lines below
        //addToImports("java.math.BigDecimal");
        // return new TypeSpecNumeric(precision, scale);
        return TypeSpecSimple.of(getJavaType(ctx, "NUMERIC"));   // ignore precision and scale for now
    }

    @Override
    public TypeSpecSimple visitString_type(String_typeContext ctx) {
        // ignore length for now
        return TypeSpecSimple.of("java.lang.String");
    }

    @Override
    public TypeSpecSimple visitSimple_type(Simple_typeContext ctx) {
        String pcsType = Misc.getNormalizedText(ctx);
        return TypeSpecSimple.of(getJavaType(ctx, pcsType));
    }

    @Override
    public Expr visitDefault_value_part(Default_value_partContext ctx) {
        if (ctx == null) {
            return null;
        }
        return visitExpression(ctx.expression());
    }

    @Override
    public Expr visitAnd_exp(And_expContext ctx) {
        Expr l = visitExpression(ctx.expression(0));
        Expr r = visitExpression(ctx.expression(1));

        return new ExprBinaryOp(ctx, "And", l, r);
    }

    @Override
    public Expr visitOr_exp(Or_expContext ctx) {
        Expr l = visitExpression(ctx.expression(0));
        Expr r = visitExpression(ctx.expression(1));

        return new ExprBinaryOp(ctx, "Or", l, r);
    }

    @Override
    public Expr visitXor_exp(Xor_expContext ctx) {
        Expr l = visitExpression(ctx.expression(0));
        Expr r = visitExpression(ctx.expression(1));

        return new ExprBinaryOp(ctx, "Xor", l, r);
    }

    @Override
    public Expr visitNot_exp(Not_expContext ctx) {
        Expr o = visitExpression(ctx.unary_logical_expression());
        return new ExprUnaryOp(ctx, "Not", o);
    }

    @Override
    public Expr visitRel_exp(Rel_expContext ctx) {
        String opStr = null;

        Relational_operatorContext op = ctx.relational_operator();
        if (op.EQUALS_OP() != null) {
            opStr = "Eq";
        } else if (op.NULL_SAFE_EQUALS_OP() != null) {
            opStr = "NullSafeEq";
        } else if (op.NOT_EQUAL_OP() != null) {
            opStr = "Neq";
        } else if (op.LE() != null) {
            opStr = "Le";
        } else if (op.GE() != null) {
            opStr = "Ge";
        } else if (op.LT() != null) {
            opStr = "Lt";
        } else if (op.GT() != null) {
            opStr = "Gt";
        } else if (op.SETEQ() != null) {
            opStr = "SetEq";
        } else if (op.SETNEQ() != null) {
            opStr = "SetNeq";
        } else if (op.SUPERSET() != null) {
            opStr = "Superset";
        } else if (op.SUBSET() != null) {
            opStr = "Subset";
        } else if (op.SUPERSETEQ() != null) {
            opStr = "SupersetEq";
        } else if (op.SUBSETEQ() != null) {
            opStr = "SubsetEq";
        }
        if (opStr == null) {
            assert false : "unreachable";   // by syntax
            throw new RuntimeException("unreachable");
        }


        String ty = null;
        if (opStr.equals("Eq") || opStr.equals("NullSafeEq") || opStr.equals("Neq")) {
            ty = "Object";
        }

        Expr l = visitExpression(ctx.relational_expression(0));
        Expr r = visitExpression(ctx.relational_expression(1));

        return new ExprBinaryOp(ctx, opStr, l, r);
    }

    @Override
    public Expr visitIs_null_exp(Is_null_expContext ctx) {
        Expr o = visitExpression(ctx.is_null_expression());

        Expr expr = new ExprUnaryOp(ctx, "IsNull", o);
        return ctx.NOT() == null ? expr : new ExprUnaryOp(ctx, "Not", expr);
    }

    @Override
    public Expr visitBetween_exp(Between_expContext ctx) {
        Expr target = visitExpression(ctx.between_expression());
        Expr lowerBound =
                visitExpression(ctx.between_elements().between_expression(0));
        Expr upperBound =
                visitExpression(ctx.between_elements().between_expression(1));

        Expr expr = new ExprBetween(ctx, target, lowerBound, upperBound);
        return ctx.NOT() == null ? expr : new ExprUnaryOp(ctx, "Not", expr);
    }

    @Override
    public Expr visitIn_exp(In_expContext ctx) {
        Expr target = visitExpression(ctx.in_expression());
        NodeList<Expr> inElements = visitIn_elements(ctx.in_elements());

        Expr expr = new ExprIn(ctx, target, inElements);
        return ctx.NOT() == null ? expr : new ExprUnaryOp(ctx, "Not", expr);
    }

    @Override
    public NodeList<Expr> visitIn_elements(In_elementsContext ctx) {

        NodeList<Expr> ret = new NodeList<>();

        for (In_expressionContext e : ctx.in_expression()) {
            ret.addNode(visitExpression(e));
        }

        return ret;
    }

    @Override
    public Expr visitLike_exp(Like_expContext ctx) {
        Expr target = visitExpression(ctx.like_expression());
        ExprStr pattern = visitQuoted_string(ctx.pattern);
        ExprStr escape = ctx.escape == null ? null : visitQuoted_string(ctx.escape);

        assert pattern != null; // by syntax

        if (escape != null && escape.val.length() != 1) {
            throw new SemanticError(Misc.getLineOf(ctx.escape),     // s002
                "the escape does not consist of a single character");
        }

        Expr expr = new ExprLike(ctx, target, pattern, escape);
        return ctx.NOT() == null ? expr : new ExprUnaryOp(ctx, "Not", expr);
    }

    @Override
    public Expr visitMult_exp(Mult_expContext ctx) {
        Expr l = visitExpression(ctx.concatenation(0));
        Expr r = visitExpression(ctx.concatenation(1));
        String opStr =
                ctx.ASTERISK() != null
                        ? "Mult"
                        : ctx.SOLIDUS() != null
                                ? "Div"
                                : ctx.DIV() != null ? "DivInt" : ctx.MOD() != null ? "Mod" : null;
        if (opStr == null) {
            assert false : "unreachable";   // by syntax
            throw new RuntimeException("unreachable");
        }

        return new ExprBinaryOp(ctx, opStr, l, r);
    }

    @Override
    public Expr visitAdd_exp(Add_expContext ctx) {
        String opStr =
                ctx.PLUS_SIGN() != null
                        ? "Add"
                        : ctx.MINUS_SIGN() != null
                                ? "Subtract"
                                : ctx.CONCAT_OP() != null ? "Concat" : null;
        if (opStr == null) {
            assert false : "unreachable";   // by syntax
            throw new RuntimeException("unreachable");
        }

        String castTy = opStr.equals("Concat") ? "Object" : "Integer";

        Expr l = visitExpression(ctx.concatenation(0));
        Expr r = visitExpression(ctx.concatenation(1));

        return new ExprBinaryOp(ctx, opStr, l, r);
    }

    @Override
    public Expr visitSign_exp(Sign_expContext ctx) {
        Expr o = visitExpression(ctx.unary_expression());

        Expr ret =
                ctx.PLUS_SIGN() != null
                        ? o
                        : ctx.MINUS_SIGN() != null ? new ExprUnaryOp(ctx, "Neg", o) : null;
        if (ret == null) {
            assert false : "unreachable";   // by syntax
            throw new RuntimeException("unreachable");
        }

        return ret;
    }

    @Override
    public Expr visitBit_compli_exp(Bit_compli_expContext ctx) {
        Expr o = visitExpression(ctx.unary_expression());
        return new ExprUnaryOp(ctx, "BitCompli", o);
    }

    @Override
    public Expr visitBit_shift_exp(Bit_shift_expContext ctx) {
        Expr l = visitExpression(ctx.concatenation(0));
        Expr r = visitExpression(ctx.concatenation(1));
        String opStr =
                ctx.LT2() != null ? "BitShiftLeft" : ctx.GT2() != null ? "BitShiftRight" : null;
        if (opStr == null) {
            assert false : "unreachable";   // by syntax
            throw new RuntimeException("unreachable");
        }

        return new ExprBinaryOp(ctx, opStr, l, r);
    }

    @Override
    public Expr visitBit_and_exp(Bit_and_expContext ctx) {
        Expr l = visitExpression(ctx.concatenation(0));
        Expr r = visitExpression(ctx.concatenation(1));
        return new ExprBinaryOp(ctx, "BitAnd", l, r);
    }

    @Override
    public Expr visitBit_xor_exp(Bit_xor_expContext ctx) {
        Expr l = visitExpression(ctx.concatenation(0));
        Expr r = visitExpression(ctx.concatenation(1));
        return new ExprBinaryOp(ctx, "BitXor", l, r);
    }

    @Override
    public Expr visitBit_or_exp(Bit_or_expContext ctx) {
        Expr l = visitExpression(ctx.concatenation(0));
        Expr r = visitExpression(ctx.concatenation(1));
        return new ExprBinaryOp(ctx, "BitOr", l, r);
    }

    @Override
    public Expr visitDate_exp(Date_expContext ctx) {
        String s = ctx.quoted_string().getText();
        s = quotedStrToJavaStr(s);
        LocalDate date = DateTimeParser.DateLiteral.parse(s);
        if (date == null) {
            throw new SemanticError(Misc.getLineOf(ctx),    // s003
                "invalid DATE string: " + s);
        }
        // System.out.println("[temp] date=" + date);
        return new ExprDate(ctx, date);
    }

    @Override
    public Expr visitTime_exp(Time_expContext ctx) {
        String s = ctx.quoted_string().getText();
        s = quotedStrToJavaStr(s);
        LocalTime time = DateTimeParser.TimeLiteral.parse(s);
        if (time == null) {
            throw new SemanticError(Misc.getLineOf(ctx),    // s004
                "invalid TIME string: " + s);
        }
        // System.out.println("[temp] time=" + time);
        return new ExprTime(ctx, time);
    }

    @Override
    public Expr visitTimestamp_exp(Timestamp_expContext ctx) {
        String s = ctx.quoted_string().getText();
        return parseZonedDateTime(ctx, s, false, "TIMESTAMP");
    }

    @Override
    public Expr visitDatetime_exp(Datetime_expContext ctx) {
        String s = ctx.quoted_string().getText();
        s = quotedStrToJavaStr(s);
        LocalDateTime datetime = DateTimeParser.DatetimeLiteral.parse(s);
        if (datetime == null) {
            throw new SemanticError(Misc.getLineOf(ctx),    // s005
                "invalid DATETIME string: " + s);
        }
        // System.out.println("[temp] datetime=" + datetime);
        return new ExprDatetime(ctx, datetime);
    }

    /* TODO: restore the following four methods
    @Override
    public Expr visitTimestamptz_exp(Timestamptz_expContext ctx) {
        String s = ctx.quoted_string().getText();
        return parseZonedDateTime(ctx, s, false, "TIMESTAMPTZ");
    }

    @Override
    public Expr visitTimestampltz_exp(Timestampltz_expContext ctx) {
        String s = ctx.quoted_string().getText();
        return parseZonedDateTime(ctx, s, false, "TIMESTAMPLTZ");
    }

    @Override
    public Expr visitDatetimetz_exp(Datetimetz_expContext ctx) {
        String s = ctx.quoted_string().getText();
        return parseZonedDateTime(ctx, s, true, "DATETIMETZ");
    }

    @Override
    public Expr visitDatetimeltz_exp(Datetimeltz_expContext ctx) {
        String s = ctx.quoted_string().getText();
        return parseZonedDateTime(ctx, s, true, "DATETIMELTZ");
    }
     */

    @Override
    public ExprUint visitUint_exp(Uint_expContext ctx) {

        try {
            TypeSpec ty;

            BigInteger bi = new BigInteger(ctx.UNSIGNED_INTEGER().getText());
            if (bi.compareTo(UINT_LITERAL_MAX) > 0) {
                throw new SemanticError(Misc.getLineOf(ctx),    // s006
                    "number of digits of integer literals may not exceed 38");
            } else if (bi.compareTo(BIGINT_MAX) > 0) {
                ty = TypeSpecSimple.BIGDECIMAL;
            } else if (bi.compareTo(INT_MAX) > 0) {
                ty = TypeSpecSimple.LONG;
            } else {
                ty = TypeSpecSimple.INTEGER;
            }

            return new ExprUint(ctx, bi.toString(), ty);
        } catch (NumberFormatException e) {
            assert false : "unreachable";   // by syntax
            throw new RuntimeException("unreachable");
        }
    }

    @Override
    public ExprFloat visitFp_num_exp(Fp_num_expContext ctx) {
        try {
            BigDecimal bd = new BigDecimal(ctx.FLOATING_POINT_NUM().getText());
            return new ExprFloat(ctx, bd.toString());
        } catch (NumberFormatException e) {
            assert false : "unreachable";   // by syntax
            throw new RuntimeException("unreachable");
        }
    }

    @Override
    public ExprStr visitStr_exp(Str_expContext ctx) {
        String val = ctx.quoted_string().getText();
        return new ExprStr(ctx, quotedStrToJavaStr(val));
    }

    @Override
    public ExprStr visitQuoted_string(Quoted_stringContext ctx) {
        String val = ctx.getText();
        return new ExprStr(ctx, quotedStrToJavaStr(val));
    }

    @Override
    public Expr visitNull_exp(Null_expContext ctx) {
        return ExprNull.SINGLETON;
    }

    @Override
    public Expr visitTrue_exp(True_expContext ctx) {
        return ExprTrue.SINGLETON;
    }

    @Override
    public Expr visitFalse_exp(False_expContext ctx) {
        return ExprFalse.SINGLETON;
    }

    @Override
    public Expr visitField_exp(Field_expContext ctx) {

        String fieldName = Misc.getNormalizedText(ctx.field);

        ExprId record = visitNonFuncIdentifier(ctx.record);
        if (record == null) {
            // NOTE: decl can be null if ctx.record is a serial
            if (fieldName.equals("CURRENT_VALUE") || fieldName.equals("NEXT_VALUE")) {

                connectionRequired = true;
                addToImports("java.sql.*");

                // do not push a symbol table: no nested structure
                return new ExprSerialVal(ctx,
                    Misc.getNormalizedText(ctx.record),
                    fieldName.equals("CURRENT_VALUE")
                            ? ExprSerialVal.SerialVal.CURR_VAL
                            : ExprSerialVal.SerialVal.NEXT_VAL);
            } else {
                throw new SemanticError(Misc.getLineOf(ctx.record), // s007
                    "undeclared id " + Misc.getNormalizedText(ctx.record));
            }
        } else {
            if (!(record.decl instanceof DeclForRecord)) {
                throw new SemanticError(Misc.getLineOf(ctx.record), // s008
                    "field lookup is only allowed for records");
            }

            Scope scope = symbolStack.getCurrentScope();
            return new ExprField(ctx, record, fieldName);
        }
    }

    @Override
    public Expr visitFunction_call(Function_callContext ctx) {

        String name = Misc.getNormalizedText(ctx.identifier());
        NodeList<Expr> args = visitFunction_argument(ctx.function_argument());

        DeclFunc decl = symbolStack.getDeclFunc(name);
        if (decl == null) {

            for (Expr arg : args.nodes) {
                if (arg instanceof ExprCast) {
                    ((ExprCast) arg).setTargetType("Object");
                }
            }

            connectionRequired = true;
            addToImports("java.sql.*");

            ExprGlobalFuncCall ret = new ExprGlobalFuncCall(ctx, name, args);

            return ret;
        } else {
            if (decl.paramList.nodes.size() != args.nodes.size()) {
                throw new SemanticError(Misc.getLineOf(ctx),    // s009
                    "the number of arguments to function " + name +
                    " does not match the number of the function's declared formal parameters");
            }

            int i = 0;
            for (Expr arg : args.nodes) {
                DeclParam dp = decl.paramList.nodes.get(i);

                if (dp instanceof DeclParamOut) {
                    if (arg instanceof ExprId && isAssignableTo((ExprId) arg)) {
                        // OK
                    } else {
                        throw new SemanticError(Misc.getLineOf(arg.ctx),    // s010
                            "argument " + i + " to the function " + name +
                            " must be assignable to because it is to an out-parameter");
                    }
                } else if (arg instanceof ExprCast) {
                    ((ExprCast) arg).setTargetType(dp.typeSpec().name);
                }

                i++;
            }

            return new ExprLocalFuncCall(ctx, name, args, symbolStack.getCurrentScope(), decl);
        }
    }

    @Override
    public Expr visitSearched_case_expression(Searched_case_expressionContext ctx) {

        NodeList<CondExpr> condParts = new NodeList<>();
        for (Searched_case_expression_when_partContext c :
                ctx.searched_case_expression_when_part()) {
            Expr cond = visitExpression(c.expression(0));
            Expr expr = visitExpression(c.expression(1));
            condParts.addNode(new CondExpr(c, cond, expr));
        }

        Expr elsePart;
        if (ctx.case_expression_else_part() == null) {
            elsePart = null;    // TODO: put exception throwing code insead of null
        } else {
            elsePart = visitExpression(ctx.case_expression_else_part().expression());
        }

        return new ExprCond(ctx, condParts, elsePart);
    }

    @Override
    public Expr visitSimple_case_expression(Simple_case_expressionContext ctx) {

        symbolStack.pushSymbolTable("case_expr", null);

        Expr selector = visitExpression(ctx.expression());

        NodeList<CaseExpr> whenParts = new NodeList<>();
        for (Simple_case_expression_when_partContext c : ctx.simple_case_expression_when_part()) {
            Expr val = visitExpression(c.expression(0));
            Expr expr = visitExpression(c.expression(1));
            whenParts.addNode(new CaseExpr(c, val, expr));
        }

        Expr elsePart;
        if (ctx.case_expression_else_part() == null) {
            elsePart = null;    // TODO: put exception throwing code insead of null
        } else {
            elsePart = visitExpression(ctx.case_expression_else_part().expression());
        }

        symbolStack.popSymbolTable();

        if (whenParts.nodes.size() > 0) {
            addToImports("java.util.Objects");
        }
        return new ExprCase(ctx, selector, whenParts, elsePart);
    }

    @Override
    public AstNode visitCursor_attr_exp(Cursor_attr_expContext ctx) {

        ExprId cursor = visitNonFuncIdentifier(ctx.cursor_exp().identifier());
        if (cursor == null) {
            throw new SemanticError(Misc.getLineOf(ctx),    // s011
                "undeclared id " + Misc.getNormalizedText(ctx.cursor_exp().identifier()));
        }
        if (!isCursorOrRefcursor(cursor)) {
            throw new SemanticError(Misc.getLineOf(ctx),    // s012
                "cursor attributes cannot be read from a non-cursor object");
        }

        ExprCursorAttr.Attr attr =
                ctx.PERCENT_ISOPEN() != null
                        ? ExprCursorAttr.Attr.ISOPEN
                        : ctx.PERCENT_FOUND() != null
                                ? ExprCursorAttr.Attr.FOUND
                                : ctx.PERCENT_NOTFOUND() != null
                                        ? ExprCursorAttr.Attr.NOTFOUND
                                        : ctx.PERCENT_ROWCOUNT() != null ? ExprCursorAttr.Attr.ROWCOUNT : null;
        assert attr != null;    // by syntax

        return new ExprCursorAttr(ctx, cursor, attr);
    }

    @Override
    public ExprSqlRowCount visitSql_rowcount_exp(Sql_rowcount_expContext ctx) {
        return new ExprSqlRowCount(ctx);
    }

    @Override
    public Expr visitParen_exp(Paren_expContext ctx) {
        return visitExpression(ctx.expression());
    }

    @Override
    public Expr visitList_exp(List_expContext ctx) {
        NodeList<Expr> elems = visitExpressions(ctx.expressions());
        addToImports("java.util.Arrays");
        return new ExprList(ctx, elems);
    }

    @Override
    public NodeList<Decl> visitSeq_of_declare_specs(Seq_of_declare_specsContext ctx) {

        if (ctx == null) {
            return null;
        }

        // scan the declarations for the procedures and functions
        // in order for the effect of their forward declarations
        for (Declare_specContext ds : ctx.declare_spec()) {

            ParserRuleContext routine;

            routine = ds.routine_definition();
            if (routine != null) {
                previsitRoutine_definition((Routine_definitionContext) routine);
            }
        }

        NodeList<Decl> ret = new NodeList<>();

        for (Declare_specContext ds : ctx.declare_spec()) {
            Decl d = (Decl) visit(ds);
            if (d != null) {
                ret.addNode(d);
            }
        }

        symbolStack.getCurrentScope().setDeclDone();

        if (ret.nodes.size() == 0) {
            return null;
        } else {
            return ret;
        }
    }

    @Override
    public AstNode visitPragma_declaration(Pragma_declarationContext ctx) {
        assert ctx.AUTONOMOUS_TRANSACTION() != null;    // by syntax

        // currently, only the Autonomous Transaction is
        // allowed only in the top-level declarations
        if (symbolStack.getCurrentScope().level != 2) {
            throw new SemanticError(Misc.getLineOf(ctx),    // s013
                "AUTONOMOUS_TRANSACTION declaration is only allowed at the top level");
        }

        // just turn on the flag and return nothing
        autonomousTransaction = true;
        return null;
    }

    @Override
    public AstNode visitConstant_declaration(Constant_declarationContext ctx) {

        String name = Misc.getNormalizedText(ctx.identifier());
        TypeSpec ty = (TypeSpec) visit(ctx.type_spec());
        Expr val = visitDefault_value_part(ctx.default_value_part());
        if (val instanceof ExprCast) {
            ((ExprCast) val).setTargetType(ty.name);
        }

        DeclConst ret = new DeclConst(ctx, name, ty, ctx.NOT() != null, val);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    @Override
    public AstNode visitException_declaration(Exception_declarationContext ctx) {

        String name = Misc.getNormalizedText(ctx.identifier());

        DeclException ret = new DeclException(ctx, name);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    @Override
    public AstNode visitVariable_declaration(Variable_declarationContext ctx) {

        String name = Misc.getNormalizedText(ctx.identifier());
        TypeSpec ty = (TypeSpec) visit(ctx.type_spec());
        Expr val = visitDefault_value_part(ctx.default_value_part());
        if (val instanceof ExprCast) {
            ((ExprCast) val).setTargetType(ty.name);
        }

        DeclVar ret = new DeclVar(ctx, name, ty, ctx.NOT() != null, val);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    @Override
    public AstNode visitCursor_definition(Cursor_definitionContext ctx) {

        String name = Misc.getNormalizedText(ctx.identifier());

        symbolStack.pushSymbolTable("cursor_def", null);

        NodeList<DeclParam> paramList = visitParameter_list(ctx.parameter_list());
        for (DeclParam dp: paramList.nodes) {
            if (dp instanceof DeclParamOut) {
                throw new SemanticError(Misc.getLineOf(dp.ctx), // s014
                    "parameters of cursor definition cannot be OUT parameters");
            }
        }

        TempSqlStringifier stringifier = new TempSqlStringifier(symbolStack);
        new ParseTreeWalker().walk(stringifier, ctx.s_select_statement());
        if (stringifier.intoVars != null) {
            throw new SemanticError(Misc.getLineOf(ctx.s_select_statement()),   // s015
                "SQL in a cursor definition cannot have an into-clause");
        }
        String sql = StringEscapeUtils.escapeJava(stringifier.sbuf.toString());

        symbolStack.popSymbolTable();

        DeclCursor ret = new DeclCursor(ctx, name, paramList,
            new ExprStr(ctx.s_select_statement(), sql), stringifier.usedVars);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    @Override
    public DeclRoutine visitRoutine_definition(Routine_definitionContext ctx) {

        String name = Misc.getNormalizedText(ctx.identifier());

        boolean isFunction = (ctx.PROCEDURE() == null);

        symbolStack.pushSymbolTable(name, isFunction ? Misc.RoutineType.FUNC : Misc.RoutineType.PROC);

        visitParameter_list(ctx.parameter_list());  // just to put symbols to the symbol table

        NodeList<Decl> decls = visitSeq_of_declare_specs(ctx.seq_of_declare_specs());
        Body body = visitBody(ctx.body());

        symbolStack.popSymbolTable();

        DeclRoutine ret;
        if (isFunction) {
            ret = symbolStack.getDeclFunc(name);
            if (!controlFlowBlocked) {
                throw new SemanticError(Misc.getLineOf(ctx),    // s016
                    "function " + ret.name + " can reach its end without returning a value");
            }
        } else {
            // procedure
            ret = symbolStack.getDeclProc(name);
        }
        assert ret != null; // by the previsit
        ret.decls = decls;
        ret.body = body;

        return ret;
    }

    @Override
    public Body visitBody(BodyContext ctx) {

        boolean allFlowsBlocked;

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        allFlowsBlocked = controlFlowBlocked;

        NodeList<ExHandler> exHandlers = new NodeList<>();
        for (Exception_handlerContext ehc : ctx.exception_handler()) {
            exHandlers.addNode(visitException_handler(ehc));
            allFlowsBlocked = allFlowsBlocked && controlFlowBlocked;
        }

        controlFlowBlocked = allFlowsBlocked;   // s017-1
        return new Body(ctx, stmts, exHandlers);
    }

    @Override
    public NodeList<Stmt> visitSeq_of_statements(Seq_of_statementsContext ctx) {

        controlFlowBlocked = false;

        NodeList<Stmt> stmts = new NodeList<>();
        for (StatementContext sc : ctx.statement()) {
            if (controlFlowBlocked) {
                throw new SemanticError(Misc.getLineOf(sc), // s017
                    "unreachable statement");
            }
            stmts.addNode((Stmt) visit(sc));
        }
        // NOTE: the last statement might turn the control flow to blocked

        return stmts;
    }

    @Override
    public StmtBlock visitBlock(BlockContext ctx) {

        symbolStack.pushSymbolTable("block", null);

        String block = symbolStack.getCurrentScope().block;

        NodeList<Decl> decls = visitSeq_of_declare_specs(ctx.seq_of_declare_specs());
        Body body = visitBody(ctx.body());

        symbolStack.popSymbolTable();

        return new StmtBlock(ctx, block, decls, body);
    }

    @Override
    public StmtAssign visitAssignment_statement(Assignment_statementContext ctx) {

        ExprId var = visitNonFuncIdentifier(ctx.identifier());
        if (var == null) {
            throw new SemanticError(Misc.getLineOf(ctx.identifier()),   // s018
                "undeclared id " + Misc.getNormalizedText(ctx.identifier()));
        }
        if (!isAssignableTo(var)) {
            throw new SemanticError(Misc.getLineOf(ctx.identifier()),   // s019
                Misc.getNormalizedText(ctx.identifier()) + " is not assignable to");
        }

        Expr val = visitExpression(ctx.expression());

        return new StmtAssign(ctx, var, val);
    }

    @Override
    public Expr visitIdentifier(IdentifierContext ctx) {
        String name = Misc.getNormalizedText(ctx);

        Decl decl = symbolStack.getDeclForIdExpr(name);
        if (decl == null) {

            // this is possibly a global function call

            connectionRequired = true;
            addToImports("java.sql.*");

            return new ExprGlobalFuncCall(ctx, name, EMPTY_ARGS);
        } else if (decl instanceof DeclId) {
            Scope scope = symbolStack.getCurrentScope();
            return new ExprId(ctx, name, scope, (DeclId) decl);
        } else if (decl instanceof DeclFunc) {
            Scope scope = symbolStack.getCurrentScope();
            return new ExprLocalFuncCall(ctx, name, null, scope, (DeclFunc) decl);
        }

        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Override
    public AstNode visitContinue_statement(Continue_statementContext ctx) {

        if (!within(ctx, Loop_statementContext.class)) {
            throw new SemanticError(Misc.getLineOf(ctx),    // s020
                "continue statements must be in a loop");
        }

        Label_nameContext lnc = ctx.label_name();
        DeclLabel declLabel;
        if (lnc == null) {
            declLabel = null;
        } else {
            String label = Misc.getNormalizedText(lnc);
            declLabel = symbolStack.getDeclLabel(label);
            if (declLabel == null) {
                throw new SemanticError(Misc.getLineOf(lnc),    // s021
                    "undeclared label " + label);
            }
        }

        if (ctx.expression() == null) {
            controlFlowBlocked = true;  // s017-2
            return new StmtContinue(ctx, declLabel);
        } else {
            Expr cond = visitExpression(ctx.expression());
            return new CondStmt(ctx, cond, new StmtContinue(ctx, declLabel));
        }
    }

    @Override
    public AstNode visitExit_statement(Exit_statementContext ctx) {

        if (!within(ctx, Loop_statementContext.class)) {
            throw new SemanticError(Misc.getLineOf(ctx),    // s022
                "exit statements must be in a loop");
        }

        DeclLabel declLabel;
        Label_nameContext lnc = ctx.label_name();
        if (lnc == null) {
            declLabel = null;
        } else {
            String label = Misc.getNormalizedText(lnc);
            declLabel = symbolStack.getDeclLabel(label);
            if (declLabel == null) {
                throw new SemanticError(Misc.getLineOf(lnc),    // s023
                    "undeclared label " + label);
            }
        }

        if (ctx.expression() == null) {
            controlFlowBlocked = true;  // s107-3
            return new StmtBreak(ctx, declLabel);
        } else {
            Expr cond = visitExpression(ctx.expression());
            return new CondStmt(ctx, cond, new StmtBreak(ctx, declLabel));
        }
    }

    @Override
    public StmtIf visitIf_statement(If_statementContext ctx) {

        boolean allFlowsBlocked;

        NodeList<CondStmt> condParts = new NodeList<>();

        Expr cond = visitExpression(ctx.expression());
        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        allFlowsBlocked = controlFlowBlocked;
        condParts.addNode(new CondStmt(ctx.expression(), cond, stmts));

        for (Elsif_partContext c : ctx.elsif_part()) {
            cond = visitExpression(c.expression());
            stmts = visitSeq_of_statements(c.seq_of_statements());
            allFlowsBlocked = allFlowsBlocked && controlFlowBlocked;
            condParts.addNode(new CondStmt(c.expression(), cond, stmts));
        }

        NodeList<Stmt> elsePart;
        if (ctx.else_part() == null) {
            elsePart = null;
            allFlowsBlocked = false;
        } else {
            elsePart = visitSeq_of_statements(ctx.else_part().seq_of_statements());
            allFlowsBlocked = allFlowsBlocked && controlFlowBlocked;
        }

        controlFlowBlocked = allFlowsBlocked;   // s017-3

        return new StmtIf(ctx, false, condParts, elsePart);
    }

    @Override
    public StmtBasicLoop visitStmt_basic_loop(Stmt_basic_loopContext ctx) {

        symbolStack.pushSymbolTable("loop", null);

        DeclLabel declLabel = visitLabel_declaration(ctx.label_declaration());
        if (declLabel != null) {
            symbolStack.putDecl(declLabel.name, declLabel);
        }

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        controlFlowBlocked = false; // every loop is assumed not to block control flow in generated Java code

        symbolStack.popSymbolTable();

        return new StmtBasicLoop(ctx, declLabel, stmts);
    }

    @Override
    public DeclLabel visitLabel_declaration(Label_declarationContext ctx) {

        if (ctx == null) {
            return null;
        }

        String name = Misc.getNormalizedText(ctx.label_name());

        return new DeclLabel(ctx, name);
    }

    @Override
    public StmtWhileLoop visitStmt_while_loop(Stmt_while_loopContext ctx) {

        symbolStack.pushSymbolTable("while", null);

        DeclLabel declLabel = visitLabel_declaration(ctx.label_declaration());
        if (declLabel != null) {
            symbolStack.putDecl(declLabel.name, declLabel);
        }

        Expr cond = visitExpression(ctx.expression());  // TODO: handle the case when the cond is compile time TRUE
        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        controlFlowBlocked = false; // every loop is assumed not to block control flow in generated Java code

        symbolStack.popSymbolTable();

        return new StmtWhileLoop(ctx, declLabel, cond, stmts);
    }

    @Override
    public StmtForIterLoop visitStmt_for_iter_loop(Stmt_for_iter_loopContext ctx) {

        symbolStack.pushSymbolTable("for_iter", null);

        String iter = Misc.getNormalizedText(ctx.iterator().index_name());

        boolean reverse = (ctx.iterator().REVERSE() != null);

        // the following must be done before putting the iterator variable to the symbol stack
        Expr lowerBound = visitLower_bound(ctx.iterator().lower_bound());
        Expr upperBound = visitUpper_bound(ctx.iterator().upper_bound());
        Expr step = visitStep(ctx.iterator().step());

        DeclForIter iterDecl = new DeclForIter(ctx.iterator().index_name(), iter);
        symbolStack.putDecl(iter, iterDecl);

        DeclLabel declLabel = visitLabel_declaration(ctx.label_declaration());
        if (declLabel != null) {
            symbolStack.putDecl(declLabel.name, declLabel);
        }

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        controlFlowBlocked = false; // every loop is assumed not to block control flow in generated Java code

        symbolStack.popSymbolTable();

        return new StmtForIterLoop(ctx,
                declLabel, iterDecl, reverse, lowerBound, upperBound, step, stmts);
    }

    @Override
    public Expr visitLower_bound(Lower_boundContext ctx) {
        return visitExpression(ctx.concatenation());
    }

    @Override
    public Expr visitUpper_bound(Upper_boundContext ctx) {
        return visitExpression(ctx.concatenation());
    }

    @Override
    public Expr visitStep(StepContext ctx) {
        if (ctx == null) {
            return null;
        }

        return visitExpression(ctx.concatenation());
    }

    @Override
    public AstNode visitStmt_for_cursor_loop(Stmt_for_cursor_loopContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        symbolStack.pushSymbolTable("for_cursor_loop", null);

        IdentifierContext idCtx = ctx.for_cursor().cursor_exp().identifier();
        ExprId cursor = visitNonFuncIdentifier(idCtx);
        if (cursor == null) {
            throw new SemanticError(Misc.getLineOf(idCtx),  // s024
                "undeclared id " + Misc.getNormalizedText(idCtx));
        }
        if (!(cursor.decl instanceof DeclCursor)) {
            throw new SemanticError(Misc.getLineOf(idCtx),  // s025
                Misc.getNormalizedText(idCtx) + " is not a cursor");
        }
        DeclCursor cursorDecl = (DeclCursor) cursor.decl;

        NodeList<Expr> args = visitExpressions(ctx.for_cursor().expressions());

        if (cursorDecl.paramList.nodes.size() != args.nodes.size()) {
            throw new SemanticError(Misc.getLineOf(idCtx),  // s026
                    "the number of arguments to cursor "
                    + Misc.getNormalizedText(idCtx)
                    + " does not match the number of its declared formal parameters");
        }

        int i = 0;
        for (Expr arg : args.nodes) {

            if (arg instanceof ExprCast) {
                DeclParam dp = cursorDecl.paramList.nodes.get(i);
                ((ExprCast) arg).setTargetType(dp.typeSpec().name);
            }

            i++;
        }

        String record = Misc.getNormalizedText(ctx.for_cursor().record_name());

        String label;
        DeclLabel declLabel = visitLabel_declaration(ctx.label_declaration());
        if (declLabel == null) {
            label = null;
        } else {
            label = declLabel.name;
            symbolStack.putDecl(label, declLabel);
        }

        DeclForRecord declForRecord = new DeclForRecord(ctx.for_cursor().record_name(), record);
        symbolStack.putDecl(record, declForRecord);

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        controlFlowBlocked = false; // every loop is assumed not to block control flow in generated Java code

        symbolStack.popSymbolTable();

        return new StmtForCursorLoop(ctx, cursor, args, label, record, stmts);
    }

    @Override
    public StmtForSqlLoop visitStmt_for_static_sql_loop(Stmt_for_static_sql_loopContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        symbolStack.pushSymbolTable("for_s_sql_loop", null);

        ParserRuleContext recNameCtx = ctx.for_static_sql().record_name();
        ParserRuleContext selectCtx = ctx.for_static_sql().s_select_statement();

        String record = Misc.getNormalizedText(recNameCtx);

        TempSqlStringifier stringifier = new TempSqlStringifier(symbolStack);
        new ParseTreeWalker().walk(stringifier, selectCtx);
        if (stringifier.intoVars != null) {
            throw new SemanticError(Misc.getLineOf(selectCtx),  // s027
                "SQL in for-loop statement cannot have an into-clause");
        }
        String sql = StringEscapeUtils.escapeJava(stringifier.sbuf.toString());

        String label;
        DeclLabel declLabel = visitLabel_declaration(ctx.label_declaration());
        if (declLabel == null) {
            label = null;
        } else {
            label = declLabel.name;
            symbolStack.putDecl(label, declLabel);
        }

        DeclForRecord declForRecord = new DeclForRecord(recNameCtx, record);
        symbolStack.putDecl(record, declForRecord);

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        controlFlowBlocked = false; // every loop is assumed not to block control flow in generated Java code

        symbolStack.popSymbolTable();

        return new StmtForSqlLoop(ctx,
                false, label, declForRecord, new ExprStr(selectCtx, sql), stringifier.usedVars, stmts);
    }

    @Override
    public StmtForSqlLoop visitStmt_for_dynamic_sql_loop(Stmt_for_dynamic_sql_loopContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        symbolStack.pushSymbolTable("for_d_sql_loop", null);

        String record = Misc.getNormalizedText(ctx.for_dynamic_sql().record_name());

        Expr dynSql = visitExpression(ctx.for_dynamic_sql().dyn_sql());

        NodeList<Expr> usedExprList;
        Restricted_using_clauseContext usingClause =
                ctx.for_dynamic_sql().restricted_using_clause();
        if (usingClause == null) {
            usedExprList = null;
        } else {
            usedExprList = visitRestricted_using_clause(usingClause);
        }

        String label;
        DeclLabel declLabel = visitLabel_declaration(ctx.label_declaration());
        if (declLabel == null) {
            label = null;
        } else {
            label = declLabel.name;
            symbolStack.putDecl(label, declLabel);
        }

        DeclForRecord declForRecord = new DeclForRecord(ctx.for_dynamic_sql().record_name(), record);
        symbolStack.putDecl(record, declForRecord);

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        controlFlowBlocked = false; // every loop is assumed not to block control flow in generated Java code

        symbolStack.popSymbolTable();

        return new StmtForSqlLoop(ctx, true, label, declForRecord, dynSql, usedExprList, stmts);
    }

    @Override
    public StmtNull visitNull_statement(Null_statementContext ctx) {
        return new StmtNull(ctx);
    }

    @Override
    public StmtRaise visitRaise_statement(Raise_statementContext ctx) {
        ExName exName = visitException_name(ctx.exception_name());
        if (exName == null) {
            if (!within(ctx, Exception_handlerContext.class)) {
                throw new SemanticError(Misc.getLineOf(ctx),    // s028
                    "raise statements without a exception name must be in an exception handler");
            }
        }

        controlFlowBlocked = true;  // s017-4
        return new StmtRaise(ctx, exName);
    }

    @Override
    public ExName visitException_name(Exception_nameContext ctx) {

        if (ctx == null) {
            return null;
        }

        String name = Misc.getNormalizedText(ctx.identifier());

        DeclException decl = symbolStack.getDeclException(name);
        if (decl == null) {
            throw new SemanticError(Misc.getLineOf(ctx),    // s029
                "undeclared exception: " + name);
        }

        Scope scope = symbolStack.getCurrentScope();

        return new ExName(ctx, name, scope, decl);
    }

    @Override
    public StmtReturn visitReturn_statement(Return_statementContext ctx) {

        controlFlowBlocked = true;  // s017-5

        Misc.RoutineType routineType = symbolStack.getCurrentScope().routineType;
        if (ctx.expression() == null) {
            if (routineType != Misc.RoutineType.PROC) {
                throw new SemanticError(Misc.getLineOf(ctx),    // s030
                    "function " + symbolStack.getCurrentScope().routine + " must return a value");
            }
            return new StmtReturn(ctx, null, null);
        } else {
            if (routineType != Misc.RoutineType.FUNC) {
                throw new SemanticError(Misc.getLineOf(ctx),    // s031
                    "procedure " + symbolStack.getCurrentScope().routine + " may not return a value");
            }

            String routine = symbolStack.getCurrentScope().routine;
            DeclFunc df = symbolStack.getDeclFunc(routine);
            assert df != null;
            return new StmtReturn(ctx, visitExpression(ctx.expression()), df.retType);
        }
    }

    @Override
    public AstNode visitSimple_case_statement(Simple_case_statementContext ctx) {

        boolean allFlowsBlocked = true;

        symbolStack.pushSymbolTable("case_stmt", null);
        int level = symbolStack.getCurrentScope().level;

        Expr selector = visitExpression(ctx.expression());

        NodeList<CaseStmt> whenParts = new NodeList<>();
        for (Simple_case_statement_when_partContext c : ctx.simple_case_statement_when_part()) {
            Expr val = visitExpression(c.expression());
            NodeList<Stmt> stmts = visitSeq_of_statements(c.seq_of_statements());
            allFlowsBlocked = allFlowsBlocked && controlFlowBlocked;
            whenParts.addNode(new CaseStmt(c, val, stmts));
        }

        NodeList<Stmt> elsePart;
        if (ctx.case_statement_else_part() == null) {
            elsePart = null;    // TODO: put exception throwing code insead of null
            //allFlowsBlocked = allFlowsBlocked && true;
        } else {
            elsePart = visitSeq_of_statements(ctx.case_statement_else_part().seq_of_statements());
            allFlowsBlocked = allFlowsBlocked && controlFlowBlocked;
        }

        symbolStack.popSymbolTable();

        if (whenParts.nodes.size() > 0) {
            addToImports("java.util.Objects");
        }

        controlFlowBlocked = allFlowsBlocked;   // s017-6
        return new StmtCase(ctx, level, selector, whenParts, elsePart);
    }

    @Override
    public StmtIf visitSearched_case_statement(Searched_case_statementContext ctx) {

        boolean allFlowsBlocked = true;

        NodeList<CondStmt> condParts = new NodeList<>();
        for (Searched_case_statement_when_partContext c : ctx.searched_case_statement_when_part()) {
            Expr cond = visitExpression(c.expression());
            NodeList<Stmt> stmts = visitSeq_of_statements(c.seq_of_statements());
            allFlowsBlocked = allFlowsBlocked && controlFlowBlocked;
            condParts.addNode(new CondStmt(c, cond, stmts));
        }

        NodeList<Stmt> elsePart;
        if (ctx.case_statement_else_part() == null) {
            elsePart = null;    // TODO: put exception throwing code insead of null
            //allFlowsBlocked = allFlowsBlocked && true;
        } else {
            elsePart = visitSeq_of_statements(ctx.case_statement_else_part().seq_of_statements());
            allFlowsBlocked = allFlowsBlocked && controlFlowBlocked;
        }

        controlFlowBlocked = allFlowsBlocked;   // s017-7
        return new StmtIf(ctx, true, condParts, elsePart);
    }

    @Override
    public StmtRaiseAppErr visitRaise_application_error_statement(
            Raise_application_error_statementContext ctx) {

        Expr errCode = visitExpression(ctx.err_code());
        Expr errMsg = visitExpression(ctx.err_msg());

        controlFlowBlocked = true;  // s017-8
        return new StmtRaiseAppErr(ctx, errCode, errMsg);
    }

    @Override
    public StmtExecImme visitData_manipulation_language_statements(
            Data_manipulation_language_statementsContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        TempSqlStringifier stringifier = new TempSqlStringifier(symbolStack);
        new ParseTreeWalker().walk(stringifier, ctx);

        // TODO: with semantic information from the server
        // if it is a SELECT statement,
        //  . error if there is no into-clause
        //  . error if identifers in the into-calause is not updatable
        //  . select list must be assignable to the identifiers in the into-clause (check their lengths and types)

        int level = symbolStack.getCurrentScope().level + 1;
        String sql = StringEscapeUtils.escapeJava(stringifier.sbuf.toString());
        return new StmtExecImme(ctx,
                false,
                level,
                new ExprStr(ctx, sql),
                stringifier.intoVars,
                stringifier.usedVars);
    }

    @Override
    public AstNode visitClose_statement(Close_statementContext ctx) {

        IdentifierContext idCtx = ctx.cursor_exp().identifier();

        ExprId cursor = visitNonFuncIdentifier(idCtx);
        if (cursor == null) {
            throw new SemanticError(Misc.getLineOf(idCtx),  // s032
                "undeclared id " + Misc.getNormalizedText(idCtx));
        }
        if (!isCursorOrRefcursor(cursor)) {
            throw new SemanticError(Misc.getLineOf(idCtx),  // s033
                "cannot close a non-cursor object");
        }

        return new StmtCursorClose(ctx, cursor);
    }

    @Override
    public AstNode visitOpen_statement(Open_statementContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        IdentifierContext idCtx = ctx.cursor_exp().identifier();

        ExprId cursor = visitNonFuncIdentifier(idCtx);
        if (cursor == null) {
            throw new SemanticError(Misc.getLineOf(idCtx),  // s034
                "undeclared id " + Misc.getNormalizedText(idCtx));
        }
        if (!(cursor.decl instanceof DeclCursor)) {
            throw new SemanticError(Misc.getLineOf(idCtx),  // s035
                Misc.getNormalizedText(idCtx) + " is not a cursor");
        }
        DeclCursor decl = (DeclCursor) cursor.decl;

        NodeList<Expr> args = visitExpressions(ctx.expressions());

        if (decl.paramList.nodes.size() != args.nodes.size()) {
            throw new SemanticError(Misc.getLineOf(ctx.expressions()),  // s036
                    "the number of arguments to cursor "
                    + Misc.getNormalizedText(idCtx)
                    + " does not match the number of its declared formal parameters");
        }

        int i = 0;
        for (Expr arg : args.nodes) {

            if (arg instanceof ExprCast) {
                DeclParam dp = decl.paramList.nodes.get(i);
                ((ExprCast) arg).setTargetType(dp.typeSpec().name);
            }

            i++;
        }

        return new StmtCursorOpen(ctx, cursor, args);
    }

    @Override
    public NodeList<Expr> visitExpressions(ExpressionsContext ctx) {

        if (ctx == null) {
            return EMPTY_ARGS;
        }

        NodeList<Expr> ret = new NodeList<>();
        for (ExpressionContext e : ctx.expression()) {
            ret.addNode(visitExpression(e));
        }

        return ret;
    }

    @Override
    public AstNode visitFetch_statement(Fetch_statementContext ctx) {

        IdentifierContext idCtx = ctx.cursor_exp().identifier();
        ExprId cursor = visitNonFuncIdentifier(idCtx);
        if (cursor == null) {
            throw new SemanticError(Misc.getLineOf(idCtx),  // s037
                "undeclared id " + Misc.getNormalizedText(idCtx));
        }
        if (!isCursorOrRefcursor(cursor)) {
            throw new SemanticError(Misc.getLineOf(idCtx),  // s038
                "cannot fetch a non-cursor object");
        }

        NodeList<ExprId> intoVars = new NodeList<>();
        for (IdentifierContext v : ctx.identifier()) {
            ExprId id = visitNonFuncIdentifier(v);
            if (!isAssignableTo(id)) {
                throw new SemanticError(Misc.getLineOf(v),  // s039
                    "variables to store fetch results must be assignable to");
            }
            intoVars.addNode(id);
        }

        return new StmtCursorFetch(ctx, cursor, intoVars);
    }

    @Override
    public AstNode visitOpen_for_statement(Open_for_statementContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        ExprId refCursor = visitNonFuncIdentifier(ctx.identifier());
        if (refCursor == null) {
            throw new SemanticError(Misc.getLineOf(ctx.identifier()),   // s040
                "undeclared id " + Misc.getNormalizedText(ctx.identifier()));
        }
        if (!isAssignableTo(refCursor)) {
            throw new SemanticError(Misc.getLineOf(ctx.identifier()),   // s041
                "identifier in a open-for statement must be assignable-to");
        }
        if (!((DeclVarLike) refCursor.decl).typeSpec().equals(TypeSpecSimple.REFCURSOR)) {
            throw new SemanticError(Misc.getLineOf(ctx.identifier()),   // s042
                "identifier in a open-for statement must be of the SYS_REFCURSOR type");
        }

        TempSqlStringifier stringifier = new TempSqlStringifier(symbolStack);
        new ParseTreeWalker().walk(stringifier, ctx.s_select_statement());
        if (stringifier.intoVars != null) {
            throw new SemanticError(Misc.getLineOf(ctx.s_select_statement()),   // s043
                "SQL in a open-for statement cannot have an into-clause");
        }
        String sql = StringEscapeUtils.escapeJava(stringifier.sbuf.toString());

        return new StmtOpenFor(ctx, refCursor, new ExprStr(ctx.s_select_statement(), sql), stringifier.usedVars);
    }

    @Override
    public StmtCommit visitCommit_statement(Commit_statementContext ctx) {
        return new StmtCommit(ctx);
    }

    @Override
    public StmtRollback visitRollback_statement(Rollback_statementContext ctx) {
        return new StmtRollback(ctx);
    }

    @Override
    public AstNode visitProcedure_call(Procedure_callContext ctx) {

        String name = Misc.getNormalizedText(ctx.routine_name());
        NodeList<Expr> args = visitFunction_argument(ctx.function_argument());

        DeclProc decl = symbolStack.getDeclProc(name);
        if (decl == null) {

            for (Expr arg : args.nodes) {
                if (arg instanceof ExprCast) {
                    ((ExprCast) arg).setTargetType("Object");
                }
            }

            connectionRequired = true;
            addToImports("java.sql.*");

            int level = symbolStack.getCurrentScope().level + 1;
            StmtGlobalProcCall ret = new StmtGlobalProcCall(ctx, level, name, args);

            return ret;
        } else {
            if (decl.paramList.nodes.size() != args.nodes.size()) {
                throw new SemanticError(Misc.getLineOf(ctx),    // s044
                    "the number of arguments to procedure " + name +
                    " does not match the number of its declared formal parameters");
            }

            int i = 0;
            for (Expr arg : args.nodes) {
                DeclParam dp = decl.paramList.nodes.get(i);

                if (dp instanceof DeclParamOut) {
                    if (arg instanceof ExprId && isAssignableTo((ExprId) arg)) {
                        // OK
                    } else {
                            throw new SemanticError(Misc.getLineOf(arg.ctx),    // s045
                                "argument " + i + " to the procedure" + name
                                + " must be a variable or out-parameter because it is to an out-parameter");
                    }

                } else if (arg instanceof ExprCast) {
                    ((ExprCast) arg).setTargetType(dp.typeSpec().name);
                }

                i++;
            }

            return new StmtLocalProcCall(ctx, name, args, symbolStack.getCurrentScope(), decl);
        }
    }

    @Override
    public StmtExecImme visitExecute_immediate(Execute_immediateContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");


        Expr dynSql = visitExpression(ctx.dyn_sql().expression());

        NodeList<ExprId> intoVarList;
        Into_clauseContext intoClause = ctx.into_clause();
        if (intoClause == null) {
            intoVarList = null;
        } else {
            intoVarList = visitInto_clause(intoClause);
        }

        NodeList<Expr> usedExprList;
        Using_clauseContext usingClause = ctx.using_clause();
        if (usingClause == null) {
            usedExprList = null;
        } else {
            usedExprList = visitUsing_clause(usingClause);
        }

        int level = symbolStack.getCurrentScope().level + 1;
        return new StmtExecImme(ctx, true, level, dynSql, intoVarList, usedExprList);
    }

    @Override
    public NodeList<Expr> visitRestricted_using_clause(Restricted_using_clauseContext ctx) {

        NodeList<Expr> ret = new NodeList<>();

        for (ExpressionContext c : ctx.expression()) {
            ret.addNode(visitExpression(c));
        }

        return ret;
    }

    @Override
    public NodeList<Expr> visitUsing_clause(Using_clauseContext ctx) {

        NodeList<Expr> ret = new NodeList<>();

        for (Using_elementContext c : ctx.using_element()) {
            Expr expr = visitExpression(c.expression());
            if (c.OUT() != null) {
                if (expr instanceof ExprId && isAssignableTo((ExprId) expr)) {
                    // OK
                } else {
                    throw new SemanticError(Misc.getLineOf(c),  // s046
                        "expression '" + c.expression().getText() +
                        "' cannot be used as an OUT parameter in the USING clause because it is not assignable to");
                }
            }
            ret.addNode(expr);
        }

        return ret;
    }

    @Override
    public NodeList<ExprId> visitInto_clause(Into_clauseContext ctx) {

        NodeList<ExprId> ret = new NodeList<>();

        for (IdentifierContext c : ctx.identifier()) {
            ExprId id = visitNonFuncIdentifier(c);
            if (id == null) {
                throw new SemanticError(Misc.getLineOf(c),  // s047
                    "undeclared id " + Misc.getNormalizedText(c));
            }
            if (!isAssignableTo(id)) {
                throw new SemanticError(Misc.getLineOf(c),  // s048
                        "variable " + Misc.getNormalizedText(c)
                        + " cannot be used in the INTO clause because it is not assignable to");
            }
            ret.addNode(id);
        }

        return ret;
    }

    @Override
    public NodeList<Expr> visitFunction_argument(Function_argumentContext ctx) {

        if (ctx == null) {
            return EMPTY_ARGS;
        }

        NodeList<Expr> ret = new NodeList<>();
        for (ArgumentContext c : ctx.argument()) {
            ret.addNode(visitExpression(c.expression()));
        }

        return ret;
    }

    @Override
    public ExHandler visitException_handler(Exception_handlerContext ctx) {

        ParserRuleContext others = null;

        List<ExName> exceptions = new ArrayList<>();
        for (Exception_nameContext c : ctx.exception_name()) {
            if ("OTHERS".equals(c.getText().toUpperCase())) {
                others = c;
                exceptions.add(new ExName(c, "OTHERS"));
            } else {
                exceptions.add(visitException_name(c));
            }
        }

        if (others != null && ctx.exception_name().size() > 1) {
            throw new SemanticError(Misc.getLineOf(others), // s049
                "OTHERS may not be combined with another exception using OR");
        }

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());

        return new ExHandler(ctx, exceptions, stmts);
    }

    public String getImportString() {
        if (imports.size() == 0) {
            return "\n// no imports";
        } else {
            StringBuffer sbuf = new StringBuffer();
            for (String s : imports) {
                sbuf.append("\nimport " + s + ";");
            }

            return sbuf.toString();
        }
    }

    // --------------------------------------------------------
    // Private
    // --------------------------------------------------------

    private static final BigInteger UINT_LITERAL_MAX = new BigInteger("99999999999999999999999999999999999999");
    private static final BigInteger BIGINT_MAX = new BigInteger("9223372036854775807");
    private static final BigInteger INT_MAX = new BigInteger("2147483648");

    private static final String SYMBOL_TABLE_TOP = "%predefined";
    private static final NodeList<DeclParam> EMPTY_PARAMS = new NodeList<>();
    private static final NodeList<Expr> EMPTY_ARGS = new NodeList<>();

    private static boolean isCursorOrRefcursor(ExprId id) {

        DeclId decl = id.decl;
        return (decl instanceof DeclCursor ||
            ((decl instanceof DeclVar || decl instanceof DeclParam) &&
                ((DeclVarLike) decl).typeSpec().equals(TypeSpecSimple.REFCURSOR)));
    }

    private static final Map<String, String> pcsToJavaTypeMap = new TreeMap<>();
    static {
        pcsToJavaTypeMap.put("BOOLEAN", "java.lang.Boolean");

        pcsToJavaTypeMap.put("CHAR", "java.lang.String");
        pcsToJavaTypeMap.put("VARCHAR", "java.lang.String");
        pcsToJavaTypeMap.put("STRING", "java.lang.String");

        pcsToJavaTypeMap.put("NUMERIC", "java.math.BigDecimal");
        pcsToJavaTypeMap.put("DECIMAL", "java.math.BigDecimal");
        pcsToJavaTypeMap.put("SHORT", "java.lang.Short");
        pcsToJavaTypeMap.put("SMALLINT", "java.lang.Short");
        pcsToJavaTypeMap.put("INT", "java.lang.Integer");
        pcsToJavaTypeMap.put("INTEGER", "java.lang.Integer");
        pcsToJavaTypeMap.put("BIGINT", "java.lang.Long");

        pcsToJavaTypeMap.put("FLOAT", "java.lang.Float");
        pcsToJavaTypeMap.put("REAL", "java.lang.Float");
        pcsToJavaTypeMap.put("DOUBLE", "java.lang.Double");
        pcsToJavaTypeMap.put("DOUBLE PRECISION", "java.lang.Double");

        pcsToJavaTypeMap.put("DATE", "java.time.LocalDate");
        pcsToJavaTypeMap.put("TIME", "java.time.LocalTime");
        pcsToJavaTypeMap.put("TIMESTAMP", "java.time.ZonedDateTime");
        pcsToJavaTypeMap.put("DATETIME", "java.time.LocalDateTime");

        /* TODO: restore the following four lines
        pcsToJavaTypeMap.put("TIMESTAMPTZ", "java.time.ZonedDateTime");
        pcsToJavaTypeMap.put("TIMESTAMPLTZ", "java.time.ZonedDateTime");
        pcsToJavaTypeMap.put("DATETIMETZ", "java.time.ZonedDateTime");
        pcsToJavaTypeMap.put("DATETIMELTZ", "java.time.ZonedDateTime");
         */

        pcsToJavaTypeMap.put("SET", "java.util.Set");
        pcsToJavaTypeMap.put("MULTISET", "org.apache.commons.collections4.MultiSet");
        pcsToJavaTypeMap.put("LIST", "java.util.List");
        pcsToJavaTypeMap.put("SEQUENCE", "java.util.List");
        pcsToJavaTypeMap.put("SYS_REFCURSOR", "com.cubrid.plcsql.predefined.sp.SpLib.Query");
    }

    private static boolean isAssignableTo(ExprId id) {
        return (id.decl instanceof DeclVar || id.decl instanceof DeclParamOut);
    }

    private static String quotedStrToJavaStr(String val) {
        val = val.substring(1, val.length() - 1); // strip enclosing '
        val = val.replace("''", "'");
        return StringEscapeUtils.escapeJava(val);
    }

    private final Set<String> imports = new TreeSet<>();

    private boolean autonomousTransaction = false;
    private boolean connectionRequired = false;

    private boolean controlFlowBlocked;

    private ExprId visitNonFuncIdentifier(IdentifierContext ctx) {
        String name = Misc.getNormalizedText(ctx);

        Decl decl = symbolStack.getDeclForIdExpr(name);
        if (decl == null) {
            return null;
        } else if (decl instanceof DeclId) {
            Scope scope = symbolStack.getCurrentScope();
            return new ExprId(ctx, name, scope, (DeclId) decl);
        } else if (decl instanceof DeclFunc) {
            return null;
        }

        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    private void previsitRoutine_definition(Routine_definitionContext ctx) {

        String name = Misc.getNormalizedText(ctx.identifier());

        // in order not to corrupt the current symbol table with the parameters
        symbolStack.pushSymbolTable("temp", null);

        NodeList<DeclParam> paramList = visitParameter_list(ctx.parameter_list());

        symbolStack.popSymbolTable();

        if (ctx.PROCEDURE() == null) {
            // function
            if (ctx.RETURN() == null) {
                throw new SemanticError(Misc.getLineOf(ctx),    // s050
                    "definition of function " + name + " must specify its return type");
            }
            TypeSpec retType = (TypeSpec) visit(ctx.type_spec());
            DeclFunc ret = new DeclFunc(ctx, name, paramList, retType);
            symbolStack.putDecl(name, ret);
        } else {
            // procedure
            if (ctx.RETURN() != null) {
                throw new SemanticError(Misc.getLineOf(ctx),    // s051
                    "definition of procedure " + name + " may not specify a return type");
            }
            DeclProc ret = new DeclProc(ctx, name, paramList);
            symbolStack.putDecl(name, ret);
        }
    }

    private void addToImports(String i) {
        //System.out.println("temp: i = " + i);
        imports.add(i);
    }

    private String getJavaType(ParserRuleContext ctx, String pcsType) {
        String javaType = pcsToJavaTypeMap.get(pcsType);
        assert javaType != null;    // by syntax
        if ("com.cubrid.plcsql.predefined.sp.SpLib.Query".equals(javaType)) {
            // no need to import Cursor now
        } else if (javaType.startsWith("java.lang.") && javaType.lastIndexOf('.') == 9) {  // 9:the index of the second '.'
            // no need to import java.lang.*
        } else {
            // if it is not in the java.lang package
            addToImports(javaType);
        }

        return javaType;
    }

    private Expr visitExpression(ParserRuleContext ctx) {
        if (ctx == null) {
            return null;
        } else {
            return (Expr) visit(ctx);
        }
    }

    private ExprZonedDateTime parseZonedDateTime(ParserRuleContext ctx,
            String s, boolean forDatetime, String originType) {

        s = quotedStrToJavaStr(s);
        ZonedDateTime timestamp = DateTimeParser.ZonedDateTimeLiteral.parse(s, forDatetime);
        if (timestamp == null) {
            throw new SemanticError(Misc.getLineOf(ctx),    // s053
                String.format("invalid %s string: %s", originType, s));
        }
        addToImports("java.time.ZoneOffset");
        if (timestamp.equals(DateTimeParser.nullDatetimeUTC)) {
            addToImports("java.time.LocalDateTime");
        }
        return new ExprZonedDateTime(ctx, timestamp, originType);
    }

    private boolean within(ParserRuleContext ctx, Class ctxClass) {
        while (true) {
            ParserRuleContext parent = ctx.getParent();
            if (parent == null) {
                return false;
            }
            if (ctxClass.isAssignableFrom(parent.getClass())) {
                return true;
            }
            ctx = parent;
        }
    }
}
