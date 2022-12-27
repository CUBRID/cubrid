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
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.LocalTime;
import java.time.ZonedDateTime;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TimeZone;
import java.util.TreeMap;
import java.util.TreeSet;
import org.antlr.v4.runtime.ParserRuleContext;
import org.antlr.v4.runtime.tree.ParseTreeWalker;
import org.apache.commons.text.StringEscapeUtils;

// parse tree --> AST converter
public class ParseTreeConverter extends PcsParserBaseVisitor<AstNode> {

    public ParseTreeConverter() {
        int level = symbolStack.pushSymbolTable(SYMBOL_TABLE_TOP, false);
        assert level == 0;

        setUpPredefined();
    }

    @Override
    public AstNode visitSql_script(Sql_scriptContext ctx) {
        AstNode ret = visitUnit_statement(ctx.unit_statement());
        assert symbolStack.getSize() == 1;
        return ret;
    }

    @Override
    public Unit visitCreate_procedure(Create_procedureContext ctx) {

        String name = ctx.identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        int level =
                symbolStack.pushSymbolTable(
                        "temp", false); // in order not to corrupt predefined symbol table

        NodeList<DeclParam> paramList = visitParameter_list(ctx.parameter_list());

        symbolStack.popSymbolTable();
        DeclProc decl = new DeclProc(name, paramList, null, null, level);
        symbolStack.putDecl(name, decl); // in order to allow recursive calls

        symbolStack.pushSymbolTable(name, true);

        visitParameter_list(
                ctx.parameter_list()); // need to do again to put the parameters to the symbol table

        decl.decls = visitSeq_of_declare_specs(ctx.seq_of_declare_specs());
        decl.body = visitBody(ctx.body());

        symbolStack.popSymbolTable();

        return new Unit(
                Unit.TargetKind.PROCEDURE,
                autonomousTransaction,
                connectionRequired,
                getImportString(),
                decl);
    }

    @Override
    public Unit visitCreate_function(Create_functionContext ctx) {

        String name = ctx.identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        int level =
                symbolStack.pushSymbolTable(
                        "temp", false); // in order not to corrupt predefined symbol table

        NodeList<DeclParam> paramList = visitParameter_list(ctx.parameter_list());
        TypeSpec retType = (TypeSpec) visit(ctx.type_spec());

        symbolStack.popSymbolTable();
        DeclFunc decl = new DeclFunc(name, paramList, retType, null, null, level);
        symbolStack.putDecl(name, decl); // in order to allow recursive calls

        symbolStack.pushSymbolTable(name, true);

        visitParameter_list(
                ctx.parameter_list()); // need to do again to put the parameters to the symbol table

        decl.decls = visitSeq_of_declare_specs(ctx.seq_of_declare_specs());
        decl.body = visitBody(ctx.body());

        symbolStack.popSymbolTable();

        return new Unit(
                Unit.TargetKind.FUNCTION,
                autonomousTransaction,
                connectionRequired,
                getImportString(),
                decl);
    }

    @Override
    public NodeList<DeclParam> visitParameter_list(Parameter_listContext ctx) {

        if (ctx == null) {
            return null;
        }

        NodeList<DeclParam> ret = new NodeList<>();

        for (ParameterContext pc : ctx.parameter()) {
            ret.addNode((DeclParam) visit(pc));
        }

        return ret;
    }

    @Override
    public DeclParamIn visitParameter_in(Parameter_inContext ctx) {
        String name = ctx.parameter_name().getText().toUpperCase();
        name = Misc.peelId(name);
        TypeSpec typeSpec = (TypeSpec) visit(ctx.type_spec());

        DeclParamIn ret = new DeclParamIn(name, typeSpec);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    @Override
    public DeclParamOut visitParameter_out(Parameter_outContext ctx) {
        String name = ctx.parameter_name().getText().toUpperCase();
        name = Misc.peelId(name);
        TypeSpec typeSpec = (TypeSpec) visit(ctx.type_spec());

        DeclParamOut ret = new DeclParamOut(name, typeSpec);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    @Override
    public TypeSpec visitType_spec(Type_specContext ctx) {
        String pcsType = ctx.native_datatype().getText().toUpperCase();
        pcsType = Misc.peelId(pcsType);
        String javaType = getJavaType(pcsType);
        assert javaType != null;
        return new TypeSpec(javaType);
    }

    @Override
    public Expr visitDefault_value_part(Default_value_partContext ctx) {
        if (ctx == null) {
            return null;
        }
        return visitExpression(ctx.expression(), null);
    }

    @Override
    public Expr visitAnd_exp(And_expContext ctx) {
        Expr l = visitExpression(ctx.expression(0), "Boolean");
        Expr r = visitExpression(ctx.expression(1), "Boolean");

        return new ExprBinaryOp("And", l, r);
    }

    @Override
    public Expr visitOr_exp(Or_expContext ctx) {
        Expr l = visitExpression(ctx.expression(0), "Boolean");
        Expr r = visitExpression(ctx.expression(1), "Boolean");

        return new ExprBinaryOp("Or", l, r);
    }

    @Override
    public Expr visitXor_exp(Xor_expContext ctx) {
        Expr l = visitExpression(ctx.expression(0), "Boolean");
        Expr r = visitExpression(ctx.expression(1), "Boolean");

        return new ExprBinaryOp("Xor", l, r);
    }

    @Override
    public Expr visitNot_exp(Not_expContext ctx) {
        Expr o = visitExpression(ctx.unary_logical_expression(), "Boolean");
        return new ExprUnaryOp("Not", o);
    }

    @Override
    public Expr visitRel_exp(Rel_expContext ctx) {
        Relational_operatorContext op = ctx.relational_operator();
        String opStr =
                op.EQUALS_OP() != null
                        ? "Eq"
                        : op.NOT_EQUAL_OP() != null
                                ? "Neq"
                                : op.LE() != null
                                        ? "Le"
                                        : op.GE() != null
                                                ? "Ge"
                                                : op.LT() != null
                                                        ? "Lt"
                                                        : op.GT() != null ? "Gt" : null;
        assert opStr != null;

        String ty;
        if (opStr.equals("Eq") || opStr.equals("Neq")) {
            ty = "Object";
        } else {
            ty = "Integer";
        }

        Expr l = visitExpression(ctx.relational_expression(0), ty);
        Expr r = visitExpression(ctx.relational_expression(1), ty);

        return new ExprBinaryOp(opStr, l, r);
    }

    @Override
    public Expr visitIs_null_exp(Is_null_expContext ctx) {
        Expr o = visitExpression(ctx.is_null_expression(), "Object");

        Expr expr = new ExprUnaryOp("IsNull", o);
        return ctx.NOT() == null ? expr : new ExprUnaryOp("Not", expr);
    }

    @Override
    public Expr visitBetween_exp(Between_expContext ctx) {
        Expr target = visitExpression(ctx.between_expression(), "Integer"); // TODO
        Expr lowerBound =
                visitExpression(ctx.between_elements().between_expression(0), "Integer"); // TODO
        Expr upperBound =
                visitExpression(ctx.between_elements().between_expression(1), "Integer"); // TODO

        Expr expr = new ExprBetween(target, lowerBound, upperBound);
        return ctx.NOT() == null ? expr : new ExprUnaryOp("Not", expr);
    }

    @Override
    public Expr visitIn_exp(In_expContext ctx) {
        Expr target = visitExpression(ctx.in_expression(), "Object");
        NodeList<Expr> inElements = visitIn_elements(ctx.in_elements());

        Expr expr = new ExprIn(target, inElements);
        return ctx.NOT() == null ? expr : new ExprUnaryOp("Not", expr);
    }

    @Override
    public NodeList<Expr> visitIn_elements(In_elementsContext ctx) {

        NodeList<Expr> ret = new NodeList<>();

        for (In_expressionContext e : ctx.in_expression()) {
            ret.addNode(visitExpression(e, "Object"));
        }

        return ret;
    }

    @Override
    public Expr visitLike_exp(Like_expContext ctx) {
        Expr target = visitExpression(ctx.like_expression(0), "String");
        Expr pattern = visitExpression(ctx.like_expression(1), "String");
        Expr escape = visitExpression(ctx.like_expression(2), "String");

        Expr expr = new ExprLike(target, pattern, escape);
        return ctx.NOT() == null ? expr : new ExprUnaryOp("Not", expr);
    }

    @Override
    public Expr visitMult_exp(Mult_expContext ctx) {
        Expr l = visitExpression(ctx.concatenation(0), "Integer");
        Expr r = visitExpression(ctx.concatenation(1), "Integer");
        String opStr =
                ctx.ASTERISK() != null
                        ? "Mult"
                        : (ctx.SOLIDUS() != null || ctx.DIV() != null)
                                ? "Div"
                                : ctx.MOD() != null ? "Mod" : null;
        assert opStr != null;

        return new ExprBinaryOp(opStr, l, r);
    }

    @Override
    public Expr visitAdd_exp(Add_expContext ctx) {
        String opStr =
                ctx.PLUS_SIGN() != null
                        ? "Add"
                        : ctx.MINUS_SIGN() != null
                                ? "Subtract"
                                : ctx.CONCAT_OP() != null ? "Concat" : null;
        assert opStr != null;

        String castTy = opStr.equals("Concat") ? "Object" : "Integer";

        Expr l = visitExpression(ctx.concatenation(0), castTy);
        Expr r = visitExpression(ctx.concatenation(1), castTy);

        return new ExprBinaryOp(opStr, l, r);
    }

    @Override
    public Expr visitPower_exp(Power_expContext ctx) {
        Expr l = visitExpression(ctx.unary_expression(0), "Integer");
        Expr r = visitExpression(ctx.unary_expression(1), "Integer");
        return new ExprBinaryOp("Power", l, r);
    }

    @Override
    public Expr visitNeg_exp(Neg_expContext ctx) {
        Expr o = visitExpression(ctx.unary_expression(), "Integer");

        Expr ret =
                ctx.PLUS_SIGN() != null
                        ? o
                        : ctx.MINUS_SIGN() != null ? new ExprUnaryOp("Neg", o) : null;
        assert ret != null;

        return ret;
    }

    @Override
    public Expr visitDate_exp(Date_expContext ctx) {
        String s = ctx.quoted_string().getText();
        s = quotedStrToJavaStr(s);
        LocalDate date = DateTimeParser.DateLiteral.parse(s);
        assert date != null : "invalid DATE string: " + s;
        // System.out.println("[temp] date=" + date);
        return new ExprDate(date);
    }

    @Override
    public Expr visitTime_exp(Time_expContext ctx) {
        String s = ctx.quoted_string().getText();
        s = quotedStrToJavaStr(s);
        LocalTime time = DateTimeParser.TimeLiteral.parse(s);
        assert time != null : "invalid TIME string: " + s;
        // System.out.println("[temp] time=" + time);
        return new ExprTime(time);
    }

    @Override
    public Expr visitTimestamp_exp(Timestamp_expContext ctx) {
        String s = ctx.quoted_string().getText();
        return parseZonedDateTime(s, false, "TIMESTAMP");
    }

    @Override
    public Expr visitDatetime_exp(Datetime_expContext ctx) {
        String s = ctx.quoted_string().getText();
        s = quotedStrToJavaStr(s);
        LocalDateTime datetime = DateTimeParser.DatetimeLiteral.parse(s);
        assert datetime != null : "invalid DATETIME string: " + s;
        // System.out.println("[temp] datetime=" + datetime);
        return new ExprDatetime(datetime);
    }

    @Override
    public Expr visitTimestamptz_exp(Timestamptz_expContext ctx) {
        String s = ctx.quoted_string().getText();
        return parseZonedDateTime(s, false, "TIMESTAMPTZ");
    }

    @Override
    public Expr visitTimestampltz_exp(Timestampltz_expContext ctx) {
        String s = ctx.quoted_string().getText();
        return parseZonedDateTime(s, false, "TIMESTAMPLTZ");
    }

    @Override
    public Expr visitDatetimetz_exp(Datetimetz_expContext ctx) {
        String s = ctx.quoted_string().getText();
        return parseZonedDateTime(s, true, "DATETIMETZ");
    }

    @Override
    public Expr visitDatetimeltz_exp(Datetimeltz_expContext ctx) {
        String s = ctx.quoted_string().getText();
        return parseZonedDateTime(s, true, "DATETIMELTZ");
    }

    @Override
    public Expr visitUint_exp(Uint_expContext ctx) {
        try {
            BigInteger bi = new BigInteger(ctx.UNSIGNED_INTEGER().getText());
            return new ExprNum(bi.toString());
        } catch (NumberFormatException e) {
            assert false : "invalid integer: " + ctx.UNSIGNED_INTEGER().getText();
            throw new RuntimeException("unreachable");
        }
    }

    @Override
    public Expr visitFp_num_exp(Fp_num_expContext ctx) {
        try {
            BigDecimal bd = new BigDecimal(ctx.FLOATING_POINT_NUM().getText());
            return new ExprNum(bd.toString());
        } catch (NumberFormatException e) {
            assert false : "invalid floating point number: " + ctx.FLOATING_POINT_NUM().getText();
            throw new RuntimeException("unreachable");
        }
    }

    @Override
    public Expr visitStr_exp(Str_expContext ctx) {
        String val = ctx.quoted_string().getText();
        return new ExprStr(quotedStrToJavaStr(val));
    }

    @Override
    public Expr visitNull_exp(Null_expContext ctx) {
        return ExprNull.instance();
    }

    @Override
    public Expr visitTrue_exp(True_expContext ctx) {
        return ExprTrue.instance();
    }

    @Override
    public Expr visitFalse_exp(False_expContext ctx) {
        return ExprFalse.instance();
    }

    @Override
    public Expr visitField_exp(Field_expContext ctx) {

        ExprId record = visitIdentifierInner(ctx.record);

        String fieldName = ctx.field.getText().toUpperCase();
        fieldName = Misc.peelId(fieldName);

        if (record.decl == null) {
            if (fieldName.equals("CURRENT_VALUE") || fieldName.equals("NEXT_VALUE")) {

                connectionRequired = true;
                addToImports("java.sql.*");

                return new ExprSerialVal(
                        symbolStack.getCurrentScope().level
                                + 1, // do not push a symbol table: no nested structure
                        record.name,
                        fieldName.equals("CURRENT_VALUE")
                                ? ExprSerialVal.SerialVal.CURR_VAL
                                : ExprSerialVal.SerialVal.NEXT_VAL);
            } else {
                assert false : ("undeclared id " + record.name);
                return null;
            }
        } else {
            assert record.decl instanceof DeclForRecord
                    : "field lookup is only allowed for a record, but "
                            + ctx.record.getText()
                            + " is not a record";

            return new ExprCast(new ExprField(record, fieldName));
        }
    }

    @Override
    public Expr visitFunction_call(Function_callContext ctx) {

        String name = ctx.identifier().getText().toUpperCase();
        name = Misc.peelId(name);
        NodeList<Expr> args = visitFunction_argument(ctx.function_argument());

        DeclFunc decl = symbolStack.getDeclFunc(name);
        if (decl == null) {

            if (args != null) {
                for (Expr arg : args.nodes) {
                    if (arg instanceof ExprCast) {
                        ((ExprCast) arg).setTargetType("Object");
                    }
                }
            }

            connectionRequired = true;
            addToImports("java.sql.*");

            int level = symbolStack.getCurrentScope().level + 1;
            ExprGlobalFuncCall ret = new ExprGlobalFuncCall(level, name, args);

            return new ExprCast(ret);
        } else {
            assert (decl.paramList != null && decl.paramList.nodes.size() > 0)
                            == (args != null && args.nodes.size() > 0)
                    : "the number of arguments to function "
                            + name
                            + " does not match the number of its declared formal parameters";

            if (args != null && args.nodes.size() > 0) {

                assert args.nodes.size() == decl.paramList.nodes.size()
                        : "the number of arguments to function "
                                + name
                                + " does not match the number of its declared formal parameters";

                int i = 0;
                for (Expr arg : args.nodes) {
                    DeclParam dp = decl.paramList.nodes.get(i);

                    if (dp instanceof DeclParamOut) {
                        boolean valid = false;
                        if (arg instanceof ExprId) {
                            ExprId id = (ExprId) arg;
                            if (id.decl instanceof DeclVar || id.decl instanceof DeclParamOut) {
                                valid = true;
                            }
                        }
                        assert valid
                                : "argument "
                                        + i
                                        + " to the function "
                                        + name
                                        + " must be a variable or "
                                        + " out-parameter because it is to an out-parameter";

                    } else if (arg instanceof ExprCast) {
                        ((ExprCast) arg).setTargetType(dp.typeSpec().name);
                    }

                    i++;
                }
            }

            return new ExprLocalFuncCall(name, args, symbolStack.getCurrentScope(), decl);
        }
    }

    @Override
    public Expr visitSearched_case_expression(Searched_case_expressionContext ctx) {

        NodeList<CondExpr> condParts = new NodeList<>();
        for (Searched_case_expression_when_partContext c :
                ctx.searched_case_expression_when_part()) {
            Expr cond = visitExpression(c.expression(0), "Boolean");
            Expr expr = visitExpression(c.expression(1), "Object");
            condParts.addNode(new CondExpr(cond, expr));
        }

        Expr elsePart;
        if (ctx.case_expression_else_part() == null) {
            elsePart = null;
        } else {
            elsePart = visitExpression(ctx.case_expression_else_part().expression(), "Object");
        }

        return new ExprCast(new ExprCond(condParts, elsePart));
    }

    @Override
    public Expr visitSimple_case_expression(Simple_case_expressionContext ctx) {

        symbolStack.pushSymbolTable("case_expr", false);
        int level = symbolStack.getCurrentScope().level;

        Expr selector = visitExpression(ctx.expression(), "Object");

        NodeList<CaseExpr> whenParts = new NodeList<>();
        for (Simple_case_expression_when_partContext c : ctx.simple_case_expression_when_part()) {
            Expr val = visitExpression(c.expression(0), "Object");
            Expr expr = visitExpression(c.expression(1), "Object");
            whenParts.addNode(new CaseExpr(val, expr));
        }

        Expr elsePart;
        if (ctx.case_expression_else_part() == null) {
            elsePart = null;
        } else {
            elsePart = visitExpression(ctx.case_expression_else_part().expression(), "Object");
        }

        symbolStack.popSymbolTable();

        if (whenParts.nodes.size() > 0) {
            addToImports("java.util.Objects");
        }
        return new ExprCast(new ExprCase(level, selector, whenParts, elsePart));
    }

    @Override
    public AstNode visitCursor_attr_exp(Cursor_attr_expContext ctx) {

        String name = ctx.cursor_exp().identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        DeclId decl = symbolStack.getDeclId(name);
        assert decl != null : ("undeclared id " + name);
        assert decl instanceof DeclCursor || decl instanceof DeclVar;

        Scope scope = symbolStack.getCurrentScope();

        String attribute =
                ctx.PERCENT_ISOPEN() != null
                        ? "isOpen"
                        : ctx.PERCENT_FOUND() != null
                                ? "found"
                                : ctx.PERCENT_NOTFOUND() != null
                                        ? "notFound"
                                        : ctx.PERCENT_ROWCOUNT() != null ? "rowCount" : null;
        assert attribute != null;

        return new ExprCursorAttr(new ExprId(name, scope, decl), attribute);
    }

    @Override
    public ExprSqlRowCount visitSql_rowcount_exp(Sql_rowcount_expContext ctx) {
        return new ExprSqlRowCount();
    }

    @Override
    public Expr visitParen_exp(Paren_expContext ctx) {
        return visitExpression(ctx.expression(), null);
    }

    @Override
    public Expr visitList_exp(List_expContext ctx) {
        NodeList<Expr> elems = visitExpressions(ctx.expressions());
        addToImports("java.util.Arrays");
        return new ExprList(elems);
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

            routine = ds.procedure_body();
            if (routine != null) {
                previsitProcedure_body((Procedure_bodyContext) routine);
            }
            routine = ds.function_body();
            if (routine != null) {
                previsitFunction_body((Function_bodyContext) routine);
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
        assert ctx.AUTONOMOUS_TRANSACTION() != null;

        // currently, only the Autonomous Transaction is
        // allowed only in the top-level declarations
        assert symbolStack.getCurrentScope().level == 1
                : "AUTONOMOUS_TRANSACTION declaration is only allowed at the top level";

        // just turn on the flag and return nothing
        autonomousTransaction = true;
        return null;
    }

    @Override
    public AstNode visitConstant_declaration(Constant_declarationContext ctx) {

        String name = ctx.identifier().getText().toUpperCase();
        name = Misc.peelId(name);
        TypeSpec ty = (TypeSpec) visit(ctx.type_spec());
        Expr val = visitDefault_value_part(ctx.default_value_part());
        if (val instanceof ExprCast) {
            ((ExprCast) val).setTargetType(ty.name);
        }

        DeclConst ret = new DeclConst(name, ty, val);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    @Override
    public AstNode visitException_declaration(Exception_declarationContext ctx) {

        String name = ctx.identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        DeclException ret = new DeclException(name);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    @Override
    public AstNode visitVariable_declaration(Variable_declarationContext ctx) {

        String name = ctx.identifier().getText().toUpperCase();
        name = Misc.peelId(name);
        TypeSpec ty = (TypeSpec) visit(ctx.type_spec());
        Expr val = visitDefault_value_part(ctx.default_value_part());
        if (val instanceof ExprCast) {
            ((ExprCast) val).setTargetType(ty.name);
        }

        DeclVar ret = new DeclVar(name, ty, val);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    @Override
    public AstNode visitCursor_definition(Cursor_definitionContext ctx) {

        String name = ctx.identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        symbolStack.pushSymbolTable("cursor_def", false);

        NodeList<DeclParam> paramList = visitParameter_list(ctx.parameter_list());
        // TODO: check if they are all in-params

        TempSqlStringifier stringifier = new TempSqlStringifier(symbolStack);
        new ParseTreeWalker().walk(stringifier, ctx.s_select_statement());
        assert stringifier.intoVars == null
                : "SQL in a cursor definition cannot have an into-clause";
        String sql = StringEscapeUtils.escapeJava(stringifier.sbuf.toString());

        symbolStack.popSymbolTable();

        DeclCursor ret = new DeclCursor(name, paramList, new ExprStr(sql), stringifier.usedVars);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    private void previsitProcedure_body(Procedure_bodyContext ctx) {

        String name = ctx.identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        // in order not to corrupt the current symbol table with the parameters
        int level = symbolStack.pushSymbolTable("temp", false);

        NodeList<DeclParam> paramList = visitParameter_list(ctx.parameter_list());

        symbolStack.popSymbolTable();

        DeclProc ret = new DeclProc(name, paramList, null, null, level);
        symbolStack.putDecl(name, ret);
    }

    @Override
    public AstNode visitProcedure_body(Procedure_bodyContext ctx) {

        String name = ctx.identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        DeclProc ret = symbolStack.getDeclProc(name);
        assert ret != null; // from the previsit

        symbolStack.pushSymbolTable(name, true);

        NodeList<DeclParam> paramList = visitParameter_list(ctx.parameter_list());
        NodeList<Decl> decls = visitSeq_of_declare_specs(ctx.seq_of_declare_specs());
        Body body = visitBody(ctx.body());

        symbolStack.popSymbolTable();

        ret.decls = decls;
        ret.body = body;

        return ret;
    }

    private void previsitFunction_body(Function_bodyContext ctx) {

        String name = ctx.identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        // in order not to corrupt the current symbol table with the parameters
        int level = symbolStack.pushSymbolTable("temp", false);

        NodeList<DeclParam> paramList = visitParameter_list(ctx.parameter_list());
        TypeSpec retType = (TypeSpec) visit(ctx.type_spec());

        symbolStack.popSymbolTable();

        DeclFunc ret = new DeclFunc(name, paramList, retType, null, null, level);
        symbolStack.putDecl(name, ret);
    }

    @Override
    public AstNode visitFunction_body(Function_bodyContext ctx) {

        String name = ctx.identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        DeclFunc ret = symbolStack.getDeclFunc(name);
        assert ret != null; // from the previsit

        symbolStack.pushSymbolTable(name, true);

        NodeList<DeclParam> paramList = visitParameter_list(ctx.parameter_list());
        TypeSpec retType = (TypeSpec) visit(ctx.type_spec());
        NodeList<Decl> decls = visitSeq_of_declare_specs(ctx.seq_of_declare_specs());
        Body body = visitBody(ctx.body());

        symbolStack.popSymbolTable();

        ret.decls = decls;
        ret.body = body;

        return ret;
    }

    @Override
    public Body visitBody(BodyContext ctx) {

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());

        NodeList<ExHandler> exHandlers = new NodeList<>();
        for (Exception_handlerContext ehc : ctx.exception_handler()) {
            exHandlers.addNode(visitException_handler(ehc));
        }

        return new Body(stmts, exHandlers);
    }

    @Override
    public NodeList<Stmt> visitSeq_of_statements(Seq_of_statementsContext ctx) {

        NodeList<Stmt> stmts = new NodeList<>();
        for (StatementContext sc : ctx.statement()) {
            Stmt s = (Stmt) visit(sc);
            if (s == null) {
                assert false;
            } else {
                stmts.addNode(s);
            }
        }

        return stmts;
    }

    @Override
    public StmtBlock visitBlock(BlockContext ctx) {

        symbolStack.pushSymbolTable("block", false);

        String block = symbolStack.getCurrentScope().block;

        NodeList<Decl> decls = visitSeq_of_declare_specs(ctx.seq_of_declare_specs());
        Body body = visitBody(ctx.body());

        symbolStack.popSymbolTable();

        return new StmtBlock(block, decls, body);
    }

    @Override
    public StmtAssign visitAssignment_statement(Assignment_statementContext ctx) {

        ExprId target = visitIdentifier(ctx.assignment_target().identifier());
        assert target.decl instanceof DeclVar || target.decl instanceof DeclParamOut
                : target.decl.typeStr()
                        + " "
                        + target.name
                        + " cannot be used as a target of an assignment";

        String targetType = target.decl.typeSpec().name;

        Expr val = visitExpression(ctx.expression(), targetType);

        return new StmtAssign(target, val);
    }

    private ExprId visitIdentifierInner(IdentifierContext ctx) {
        String name = ctx.getText().toUpperCase();
        name = Misc.peelId(name);

        DeclId decl =
                symbolStack.getDeclId(name); // NOTE: decl can be legally null if name is a serial
        Scope scope = symbolStack.getCurrentScope();

        return new ExprId(name, scope, decl);
    }

    @Override
    public ExprId visitIdentifier(IdentifierContext ctx) {

        ExprId e = visitIdentifierInner(ctx);
        assert e.decl != null : ("undeclared id " + e.name);
        return e;
    }

    @Override
    public AstNode visitContinue_statement(Continue_statementContext ctx) {

        Label_nameContext lnc = ctx.label_name();
        DeclLabel declLabel;
        if (lnc == null) {
            declLabel = null;
        } else {
            String label = Misc.peelId(lnc.getText().toUpperCase());
            declLabel = symbolStack.getDeclLabel(label);
            assert declLabel != null : "undeclared label " + label;
        }

        if (ctx.expression() == null) {
            return new StmtContinue(declLabel);
        } else {
            Expr cond = visitExpression(ctx.expression(), "Boolean");
            return new CondStmt(cond, new StmtContinue(declLabel));
        }
    }

    @Override
    public AstNode visitExit_statement(Exit_statementContext ctx) {

        Label_nameContext lnc = ctx.label_name();
        DeclLabel declLabel;
        if (lnc == null) {
            declLabel = null;
        } else {
            String label = Misc.peelId(lnc.getText().toUpperCase());
            declLabel = symbolStack.getDeclLabel(label);
            assert declLabel != null : "undeclared label " + label;
        }

        if (ctx.expression() == null) {
            return new StmtBreak(declLabel);
        } else {
            Expr cond = visitExpression(ctx.expression(), "Boolean");
            return new CondStmt(cond, new StmtBreak(declLabel));
        }
    }

    @Override
    public StmtIf visitIf_statement(If_statementContext ctx) {

        NodeList<CondStmt> condParts = new NodeList<>();

        Expr cond = visitExpression(ctx.expression(), "Boolean");
        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        condParts.addNode(new CondStmt(cond, stmts));

        for (Elsif_partContext c : ctx.elsif_part()) {
            cond = visitExpression(c.expression(), "Boolean");
            stmts = visitSeq_of_statements(c.seq_of_statements());
            condParts.addNode(new CondStmt(cond, stmts));
        }

        NodeList<Stmt> elsePart;
        if (ctx.else_part() == null) {
            elsePart = null;
        } else {
            elsePart = visitSeq_of_statements(ctx.else_part().seq_of_statements());
        }

        return new StmtIf(condParts, elsePart);
    }

    @Override
    public StmtBasicLoop visitStmt_basic_loop(Stmt_basic_loopContext ctx) {

        symbolStack.pushSymbolTable("loop", false);

        DeclLabel declLabel = visitLabel_declaration(ctx.label_declaration());
        if (declLabel != null) {
            symbolStack.putDecl(declLabel.name, declLabel);
        }

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());

        symbolStack.popSymbolTable();

        return new StmtBasicLoop(declLabel, stmts);
    }

    @Override
    public DeclLabel visitLabel_declaration(Label_declarationContext ctx) {

        if (ctx == null) {
            return null;
        }

        String name = ctx.label_name().getText().toUpperCase();
        name = Misc.peelId(name);

        return new DeclLabel(name);
    }

    @Override
    public StmtWhileLoop visitStmt_while_loop(Stmt_while_loopContext ctx) {

        symbolStack.pushSymbolTable("while", false);

        DeclLabel declLabel = visitLabel_declaration(ctx.label_declaration());
        if (declLabel != null) {
            symbolStack.putDecl(declLabel.name, declLabel);
        }

        Expr cond = visitExpression(ctx.expression(), "Boolean");
        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());

        symbolStack.popSymbolTable();

        return new StmtWhileLoop(declLabel, cond, stmts);
    }

    @Override
    public StmtForIterLoop visitStmt_for_iter_loop(Stmt_for_iter_loopContext ctx) {

        symbolStack.pushSymbolTable("for_iter", false);
        int level = symbolStack.getCurrentScope().level;

        String iter = ctx.iterator().index_name().getText().toUpperCase();
        iter = Misc.peelId(iter);

        boolean reverse = (ctx.iterator().REVERSE() != null);

        // the following must be done before putting the iterator variable to the symbol stack
        Expr lowerBound = visitLower_bound(ctx.iterator().lower_bound());
        Expr upperBound = visitUpper_bound(ctx.iterator().upper_bound());
        Expr step = visitStep(ctx.iterator().step());

        DeclForIter iterDecl = new DeclForIter(iter);
        symbolStack.putDecl(iter, iterDecl);

        DeclLabel declLabel = visitLabel_declaration(ctx.label_declaration());
        if (declLabel != null) {
            symbolStack.putDecl(declLabel.name, declLabel);
        }

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());

        symbolStack.popSymbolTable();

        return new StmtForIterLoop(
                level, declLabel, iter, reverse, lowerBound, upperBound, step, stmts);
    }

    @Override
    public Expr visitLower_bound(Lower_boundContext ctx) {
        return visitExpression(ctx.concatenation(), "Integer");
    }

    @Override
    public Expr visitUpper_bound(Upper_boundContext ctx) {
        return visitExpression(ctx.concatenation(), "Integer");
    }

    @Override
    public Expr visitStep(StepContext ctx) {
        if (ctx == null) {
            return null;
        }

        return visitExpression(ctx.concatenation(), "Integer");
    }

    @Override
    public AstNode visitStmt_for_cursor_loop(Stmt_for_cursor_loopContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        String cursorName = ctx.for_cursor().cursor_exp().identifier().getText().toUpperCase();
        cursorName = Misc.peelId(cursorName);

        DeclId d = symbolStack.getDeclId(cursorName);
        assert d != null : ("undeclared id " + cursorName);
        assert d instanceof DeclCursor : (cursorName + " is not a cursor");
        DeclCursor cursorDecl = (DeclCursor) d;

        Scope scope = symbolStack.getCurrentScope();

        NodeList<Expr> args = visitExpressions(ctx.for_cursor().expressions());

        assert (cursorDecl.paramList != null && cursorDecl.paramList.nodes.size() > 0)
                        == (args != null && args.nodes.size() > 0)
                : "the number of arguments to cursor "
                        + cursorName
                        + " does not match the number of its declared formal parameters";

        if (args != null && args.nodes.size() > 0) {

            assert args.nodes.size() == cursorDecl.paramList.nodes.size()
                    : "the number of arguments to cursor "
                            + cursorName
                            + " does not match the number of its declared formal parameters";

            int i = 0;
            for (Expr arg : args.nodes) {

                if (arg instanceof ExprCast) {
                    DeclParam dp = cursorDecl.paramList.nodes.get(i);
                    ((ExprCast) arg).setTargetType(dp.typeSpec().name);
                }

                i++;
            }
        }

        symbolStack.pushSymbolTable("for_cursor_loop", false);
        int level = symbolStack.getCurrentScope().level;

        String record = ctx.for_cursor().record_name().getText().toUpperCase();
        record = Misc.peelId(record);

        String label;
        DeclLabel declLabel = visitLabel_declaration(ctx.label_declaration());
        if (declLabel == null) {
            label = null;
        } else {
            label = declLabel.name;
            symbolStack.putDecl(label, declLabel);
        }

        DeclForRecord declForRecord = new DeclForRecord(record);
        symbolStack.putDecl(record, declForRecord);

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());

        symbolStack.popSymbolTable();

        return new StmtForCursorLoop(
                level, new ExprId(cursorName, scope, cursorDecl), args, label, record, stmts);
    }

    @Override
    public StmtForSqlLoop visitStmt_for_static_sql_loop(Stmt_for_static_sql_loopContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        symbolStack.pushSymbolTable("for_s_sql_loop", false);
        int level = symbolStack.getCurrentScope().level;

        String record = ctx.for_static_sql().record_name().getText().toUpperCase();
        record = Misc.peelId(record);

        TempSqlStringifier stringifier = new TempSqlStringifier(symbolStack);
        new ParseTreeWalker().walk(stringifier, ctx.for_static_sql().s_select_statement());
        assert stringifier.intoVars == null : "SQL in for-loop statement cannot have into-clause";
        String sql = StringEscapeUtils.escapeJava(stringifier.sbuf.toString());

        String label;
        DeclLabel declLabel = visitLabel_declaration(ctx.label_declaration());
        if (declLabel == null) {
            label = null;
        } else {
            label = declLabel.name;
            symbolStack.putDecl(label, declLabel);
        }

        DeclForRecord declForRecord = new DeclForRecord(record);
        symbolStack.putDecl(record, declForRecord);

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());

        symbolStack.popSymbolTable();

        return new StmtForSqlLoop(
                false, level, label, record, new ExprStr(sql), stringifier.usedVars, stmts);
    }

    @Override
    public StmtForSqlLoop visitStmt_for_dynamic_sql_loop(Stmt_for_dynamic_sql_loopContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        symbolStack.pushSymbolTable("for_d_sql_loop", false);
        int level = symbolStack.getCurrentScope().level;

        String record = ctx.for_dynamic_sql().record_name().getText().toUpperCase();
        record = Misc.peelId(record);

        Expr dynSql = visitExpression(ctx.for_dynamic_sql().dyn_sql(), "String");

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

        DeclForRecord declForRecord = new DeclForRecord(record);
        symbolStack.putDecl(record, declForRecord);

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());

        symbolStack.popSymbolTable();

        return new StmtForSqlLoop(true, level, label, record, dynSql, usedExprList, stmts);
    }

    @Override
    public StmtNull visitNull_statement(Null_statementContext ctx) {
        return new StmtNull();
    }

    @Override
    public StmtRaise visitRaise_statement(Raise_statementContext ctx) {
        ExName exName = visitException_name(ctx.exception_name());
        return new StmtRaise(exName);
    }

    @Override
    public ExName visitException_name(Exception_nameContext ctx) {

        if (ctx == null) {
            return null;
        }

        String name = ctx.identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        DeclException decl = symbolStack.getDeclException(name);
        assert decl != null : ("undeclared exception: " + name);

        Scope scope = symbolStack.getCurrentScope();

        return new ExName(name, scope, decl);
    }

    @Override
    public StmtReturn visitReturn_statement(Return_statementContext ctx) {

        if (ctx.expression() == null) {
            return new StmtReturn(null);
        } else {

            String routine = symbolStack.getCurrentScope().routine;
            DeclFunc df = symbolStack.getDeclFunc(routine);
            assert df != null;
            return new StmtReturn(visitExpression(ctx.expression(), df.retType.name));
        }
    }

    @Override
    public AstNode visitSimple_case_statement(Simple_case_statementContext ctx) {

        symbolStack.pushSymbolTable("case_stmt", false);
        int level = symbolStack.getCurrentScope().level;

        Expr selector = visitExpression(ctx.expression(), "Object");

        NodeList<CaseStmt> whenParts = new NodeList<>();
        for (Simple_case_statement_when_partContext c : ctx.simple_case_statement_when_part()) {
            Expr val = visitExpression(c.expression(), "Object");
            NodeList<Stmt> stmts = visitSeq_of_statements(c.seq_of_statements());
            whenParts.addNode(new CaseStmt(val, stmts));
        }

        NodeList<Stmt> elsePart;
        if (ctx.case_statement_else_part() == null) {
            elsePart = null;
        } else {
            elsePart = visitSeq_of_statements(ctx.case_statement_else_part().seq_of_statements());
        }

        symbolStack.popSymbolTable();

        if (whenParts.nodes.size() > 0) {
            addToImports("java.util.Objects");
        }
        return new StmtCase(level, selector, whenParts, elsePart);
    }

    @Override
    public StmtIf visitSearched_case_statement(Searched_case_statementContext ctx) {

        NodeList<CondStmt> condParts = new NodeList<>();
        for (Searched_case_statement_when_partContext c : ctx.searched_case_statement_when_part()) {
            Expr cond = visitExpression(c.expression(), "Boolean");
            NodeList<Stmt> stmts = visitSeq_of_statements(c.seq_of_statements());
            condParts.addNode(new CondStmt(cond, stmts));
        }

        NodeList<Stmt> elsePart;
        if (ctx.case_statement_else_part() == null) {
            elsePart = null;
        } else {
            elsePart = visitSeq_of_statements(ctx.case_statement_else_part().seq_of_statements());
        }

        return new StmtIf(condParts, elsePart);
    }

    @Override
    public StmtRaiseAppErr visitRaise_application_error_statement(
            Raise_application_error_statementContext ctx) {
        Expr errCode = visitExpression(ctx.err_code(), "Integer");
        Expr errMsg = visitExpression(ctx.err_msg(), "String");
        return new StmtRaiseAppErr(errCode, errMsg);
    }

    @Override
    public StmtExecImme visitData_manipulation_language_statements(
            Data_manipulation_language_statementsContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        TempSqlStringifier stringifier = new TempSqlStringifier(symbolStack);
        new ParseTreeWalker().walk(stringifier, ctx);

        String sql = StringEscapeUtils.escapeJava(stringifier.sbuf.toString());
        return new StmtExecImme(
                false,
                symbolStack.getCurrentScope().level
                        + 1, // do not push a symbol table because there is no nested structure
                new ExprStr(sql),
                stringifier.intoVars,
                stringifier.usedVars);
    }

    @Override
    public AstNode visitClose_statement(Close_statementContext ctx) {

        String name = ctx.cursor_exp().identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        DeclId decl = symbolStack.getDeclId(name);
        assert decl != null : ("undeclared id " + name);
        assert decl instanceof DeclCursor || decl instanceof DeclVar;

        Scope scope = symbolStack.getCurrentScope();

        return new StmtCursorClose(new ExprId(name, scope, decl));
    }

    @Override
    public AstNode visitOpen_statement(Open_statementContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        String name = ctx.cursor_exp().identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        DeclId d = symbolStack.getDeclId(name);
        assert d != null : ("undeclared id " + name);
        assert d instanceof DeclCursor : (name + " is not a cursor");
        DeclCursor decl = (DeclCursor) d;

        Scope scope = symbolStack.getCurrentScope();

        NodeList<Expr> args = visitExpressions(ctx.expressions());

        assert (decl.paramList != null && decl.paramList.nodes.size() > 0)
                        == (args != null && args.nodes.size() > 0)
                : "the number of arguments to cursor "
                        + name
                        + " does not match the number of its declared formal parameters";

        if (args != null && args.nodes.size() > 0) {

            assert args.nodes.size() == decl.paramList.nodes.size()
                    : "the number of arguments to cursor "
                            + name
                            + " does not match the number of its declared formal parameters";

            int i = 0;
            for (Expr arg : args.nodes) {

                if (arg instanceof ExprCast) {
                    DeclParam dp = decl.paramList.nodes.get(i);
                    ((ExprCast) arg).setTargetType(dp.typeSpec().name);
                }

                i++;
            }
        }

        return new StmtCursorOpen(scope.level, new ExprId(name, scope, decl), args);
    }

    @Override
    public NodeList<Expr> visitExpressions(ExpressionsContext ctx) {

        if (ctx == null) {
            return null;
        }

        NodeList<Expr> ret = new NodeList<>();
        for (ExpressionContext e : ctx.expression()) {
            ret.addNode(visitExpression(e, null));
        }

        return ret;
    }

    @Override
    public AstNode visitFetch_statement(Fetch_statementContext ctx) {

        String name = ctx.cursor_exp().identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        DeclId decl = symbolStack.getDeclId(name);
        assert decl != null : ("undeclared id " + name);
        assert decl instanceof DeclCursor || decl instanceof DeclVar;

        Scope scope = symbolStack.getCurrentScope();

        NodeList<ExprId> intoVars = new NodeList<>();
        for (Variable_nameContext v : ctx.variable_name()) {
            String varName = v.identifier().getText().toUpperCase();
            varName = Misc.peelId(varName);

            DeclId varDecl = symbolStack.getDeclId(varName);
            assert varDecl != null : ("undeclared id " + name);
            assert varDecl instanceof DeclParamOut || varDecl instanceof DeclVar;
            intoVars.addNode(new ExprId(varName, scope, varDecl));
        }

        return new StmtCursorFetch(new ExprId(name, scope, decl), intoVars);
    }

    @Override
    public AstNode visitOpen_for_statement(Open_for_statementContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        String name = ctx.variable_name().identifier().getText().toUpperCase();
        name = Misc.peelId(name);

        DeclId decl = symbolStack.getDeclId(name);
        assert decl != null : ("undeclared id " + name);
        assert decl instanceof DeclVar || decl instanceof DeclParamOut
                : "identifier in a open-for statement must be a variable or out-parameter";
        assert "Query".equals(decl.typeSpec().name)
                : "identifier in a open-for statement must be of the SYS_REFCURSOR type";

        Scope scope = symbolStack.getCurrentScope();

        TempSqlStringifier stringifier = new TempSqlStringifier(symbolStack);
        new ParseTreeWalker().walk(stringifier, ctx.s_select_statement());
        assert stringifier.intoVars == null
                : "SQL in a open-for statement cannot have an into-clause";
        String sql = StringEscapeUtils.escapeJava(stringifier.sbuf.toString());

        return new StmtOpenFor(
                new ExprId(name, scope, decl), new ExprStr(sql), stringifier.usedVars);
    }

    @Override
    public StmtCommit visitCommit_statement(Commit_statementContext ctx) {
        return new StmtCommit();
    }

    @Override
    public StmtRollback visitRollback_statement(Rollback_statementContext ctx) {
        return new StmtRollback();
    }

    @Override
    public AstNode visitProcedure_call(Procedure_callContext ctx) {

        String name = ctx.routine_name().getText().toUpperCase();
        name = Misc.peelId(name);
        NodeList<Expr> args = visitFunction_argument(ctx.function_argument());

        DeclProc decl = symbolStack.getDeclProc(name);
        if (decl == null) {

            if (args != null) {
                for (Expr arg : args.nodes) {
                    if (arg instanceof ExprCast) {
                        ((ExprCast) arg).setTargetType("Object");
                    }
                }
            }

            connectionRequired = true;
            addToImports("java.sql.*");

            int level = symbolStack.getCurrentScope().level + 1;
            StmtGlobalProcCall ret = new StmtGlobalProcCall(level, name, args);

            return ret;
        } else {
            assert (decl.paramList != null && decl.paramList.nodes.size() > 0)
                            == (args != null && args.nodes.size() > 0)
                    : "the number of arguments to procedure "
                            + name
                            + " does not match the number of its declared formal parameters";

            if (args != null && args.nodes.size() > 0) {

                assert args.nodes.size() == decl.paramList.nodes.size()
                        : "the number of arguments to procedure "
                                + name
                                + " does not match the number of its declared formal parameters";

                int i = 0;
                for (Expr arg : args.nodes) {
                    DeclParam dp = decl.paramList.nodes.get(i);

                    if (dp instanceof DeclParamOut) {
                        boolean valid = false;
                        if (arg instanceof ExprId) {
                            ExprId id = (ExprId) arg;
                            if (id.decl instanceof DeclVar || id.decl instanceof DeclParamOut) {
                                valid = true;
                            }
                        }
                        assert valid
                                : "argument "
                                        + i
                                        + " to the procedure"
                                        + name
                                        + " must be a variable or "
                                        + " out-parameter because it is to an out-parameter";

                    } else if (arg instanceof ExprCast) {
                        ((ExprCast) arg).setTargetType(dp.typeSpec().name);
                    }

                    i++;
                }
            }

            return new StmtLocalProcCall(name, args, symbolStack.getCurrentScope(), decl);
        }
    }

    @Override
    public StmtExecImme visitExecute_immediate(Execute_immediateContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        symbolStack.pushSymbolTable("exec_imme", false);
        int level = symbolStack.getCurrentScope().level;

        Expr dynSql = visitExpression(ctx.dyn_sql().expression(), "String");

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

        symbolStack.popSymbolTable();

        return new StmtExecImme(true, level, dynSql, intoVarList, usedExprList);
    }

    @Override
    public NodeList<Expr> visitRestricted_using_clause(Restricted_using_clauseContext ctx) {

        NodeList<Expr> ret = new NodeList<>();

        for (ExpressionContext c : ctx.expression()) {
            ret.addNode(visitExpression(c, "Object"));
        }

        return ret;
    }

    @Override
    public NodeList<Expr> visitUsing_clause(Using_clauseContext ctx) {

        NodeList<Expr> ret = new NodeList<>();

        for (Using_elementContext c : ctx.using_element()) {
            Expr expr = visitExpression(c.expression(), "Object");
            if (c.OUT() != null) {
                assert isAssignableTo(expr)
                        : "expression '"
                                + c.expression().getText()
                                + "' cannot be used as an OUT parameter in the USING clause because it is not assignable to";
            }
            ret.addNode(expr);
        }

        return ret;
    }

    @Override
    public NodeList<ExprId> visitInto_clause(Into_clauseContext ctx) {

        NodeList<ExprId> ret = new NodeList<>();

        for (IdentifierContext c : ctx.identifier()) {
            ExprId id = visitIdentifier(c);
            assert id.decl instanceof DeclVar || id.decl instanceof DeclParamOut
                    : "variable "
                            + id.name
                            + " cannot be used in the INTO clause because it is not assignable to";
            ret.addNode(id);
        }

        return ret;
    }

    @Override
    public NodeList<Expr> visitFunction_argument(Function_argumentContext ctx) {

        if (ctx == null) {
            return null;
        }

        NodeList<Expr> ret = new NodeList<>();
        for (ArgumentContext c : ctx.argument()) {
            ret.addNode(visitExpression(c.expression(), null));
        }

        return ret;
    }

    @Override
    public ExHandler visitException_handler(Exception_handlerContext ctx) {

        List<ExName> exceptions = new ArrayList<>();
        for (Exception_nameContext c : ctx.exception_name()) {
            if ("OTHERS".equals(c.getText().toUpperCase())) {
                exceptions.add(new ExName("OTHERS", null, null));
            } else {
                exceptions.add(visitException_name(c));
            }
        }

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());

        return new ExHandler(exceptions, stmts);
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

    private final SymbolStack symbolStack = new SymbolStack();
    private final Set<String> imports = new TreeSet<>();

    private static final String SYMBOL_TABLE_TOP = "%top";

    private static List<String> predefinedExceptions =
            Arrays.asList(
                    "$APP_ERROR", // for raise_application_error
                    "CASE_NOT_FOUND",
                    "CURSOR_ALREADY_OPEN",
                    "DUP_VAL_ON_INDEX",
                    "INVALID_CURSOR",
                    "LOGIN_DENIED",
                    "NO_DATA_FOUND",
                    "PROGRAM_ERROR",
                    "ROWTYPE_MISMATCH",
                    "STORAGE_ERROR",
                    "TOO_MANY_ROWS",
                    "VALUE_ERROR",
                    "ZERO_DIVIDE");

    private void setUpPredefined() {

        // add exceptions
        DeclException de;
        for (String s : predefinedExceptions) {
            de = new DeclException(s);
            symbolStack.putDecl(de.name, de);
        }

        // add procedures
        DeclProc dp =
                new DeclProc(
                        "PUT_LINE",
                        new NodeList<DeclParam>()
                                .addNode(new DeclParamIn("s", new TypeSpec("Object"))),
                        null,
                        null,
                        0);
        symbolStack.putDecl("PUT_LINE", dp);

        // add functions
        DeclFunc df = new DeclFunc("OPEN_CURSOR", null, new TypeSpec("Integer"), null, null, 0);
        symbolStack.putDecl("OPEN_CURSOR", df);

        df = new DeclFunc("LAST_ERROR_POSITION", null, new TypeSpec("Integer"), null, null, 0);
        symbolStack.putDecl("LAST_ERROR_POSITION", df);

        // add constants TODO implement SQLERRM and SQLCODE properly
        DeclConst dc = new DeclConst("SQLERRM", new TypeSpec("String"), ExprNull.instance());
        symbolStack.putDecl("SQLERRM", dc);

        dc = new DeclConst("SQLCODE", new TypeSpec("Integer"), ExprNull.instance());
        symbolStack.putDecl("SQLCODE", dc);

        dc = new DeclConst("SYSDATE", new TypeSpec("Date"), ExprNull.instance());
        symbolStack.putDecl("SYSDATE", dc);

        dc = new DeclConst("NATIVE", new TypeSpec("Integer"), ExprNull.instance());
        symbolStack.putDecl("NATIVE", dc);

        dc = new DeclConst("SQL", new TypeSpec("ResultSet"), ExprNull.instance());
        symbolStack.putDecl("SQL", dc);
    }

    private void addToImports(String i) {
        imports.add(i);
    }

    private boolean autonomousTransaction = false;
    private boolean connectionRequired = false; // TODO: temporary

    private String getJavaType(String pcsType) {
        String val = pcsToJavaTypeMap.get(pcsType);
        assert val != null : ("invalid type name " + pcsType);

        String[] split = val.split("\\.");
        if ("Query".equals(val)) {
            // no need to import Cursor now   TODO: remove this case later
        } else if (val.startsWith("java.lang.") && split.length == 3) {
            // no need to import java.lang.*
        } else {
            // if it is not in the java.lang package
            addToImports(val);
        }

        return split[split.length - 1];
    }

    /*
    private Expr
    visitExpression(ParserRuleContext ctx) {
        return visitExpression(ctx, null);
    }
     */

    private Expr visitExpression(ParserRuleContext ctx, String targetType) {
        if (ctx == null) {
            return null;
        }

        if (targetType == null) {
            return (Expr) visit(ctx);
        } else {
            Expr e = (Expr) visit(ctx);
            if (e instanceof ExprCast) {
                ((ExprCast) e).setTargetType(targetType);
            }
            return e;
        }
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
        pcsToJavaTypeMap.put("TIMESTAMPTZ", "java.time.ZonedDateTime");
        pcsToJavaTypeMap.put("TIMESTAMPLTZ", "java.time.ZonedDateTime");
        pcsToJavaTypeMap.put("DATETIMETZ", "java.time.ZonedDateTime");
        pcsToJavaTypeMap.put("DATETIMELTZ", "java.time.ZonedDateTime");
        pcsToJavaTypeMap.put("SET", "java.util.Set");
        pcsToJavaTypeMap.put("MULTISET", "org.apache.commons.collections4.MultiSet");
        pcsToJavaTypeMap.put("LIST", "java.util.List");
        pcsToJavaTypeMap.put("SEQUENCE", "java.util.List");
        pcsToJavaTypeMap.put("SYS_REFCURSOR", "Query");
    }

    private static boolean isAssignableTo(Expr expr) {

        if (expr instanceof ExprId) {
            ExprId id = (ExprId) expr;
            if (id.decl instanceof DeclVar || id.decl instanceof DeclParamOut) {
                return true;
            }
        }

        return false;
    }

    private static String quotedStrToJavaStr(String val) {
        val = val.substring(1, val.length() - 1); // strip enclosing '
        val = val.replace("''", "'");
        return StringEscapeUtils.escapeJava(val);
    }

    private ExprZonedDateTime parseZonedDateTime(String s, boolean forDatetime, String originType) {
        s = quotedStrToJavaStr(s);
        ZonedDateTime timestamp = DateTimeParser.ZonedDateTimeLiteral.parse(s, forDatetime);
        assert timestamp != null : String.format("invalid %s string: %s", originType, s);
        addToImports("java.time.ZoneOffset");
        if (timestamp.equals(DateTimeParser.nullDatetimeUTC)) {
            addToImports("java.time.LocalDateTime");
        }
        return new ExprZonedDateTime(timestamp, originType);
    }
}
