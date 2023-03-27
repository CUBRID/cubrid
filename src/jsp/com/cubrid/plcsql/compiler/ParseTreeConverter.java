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

import com.cubrid.jsp.data.ColumnInfo;
import com.cubrid.jsp.data.DBType;
import com.cubrid.plcsql.compiler.antlrgen.PcsParserBaseVisitor;
import com.cubrid.plcsql.compiler.ast.*;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.LocalTime;
import java.time.ZonedDateTime;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;
import org.antlr.v4.runtime.ParserRuleContext;
import org.apache.commons.text.StringEscapeUtils;

// parse tree --> AST converter
public class ParseTreeConverter extends PcsParserBaseVisitor<AstNode> {

    public final SymbolStack symbolStack = new SymbolStack();

    public ParseTreeConverter(Map<ParserRuleContext, SqlSemantics> staticSqls) {
        this.staticSqls = staticSqls;
    }

    public void askServerSemanticQuestions() {
        List<ServerAPI.Question> questions = new ArrayList(semanticQuestions.values());
        List<ServerAPI.Question> answered =
                ServerAPI.getGlobalSemantics(questions); // this may take a long time

        int seqNo = -1;
        Iterator<AstNode> iterNodes = semanticQuestions.keySet().iterator();
        for (ServerAPI.Question q : answered) {

            assert q.seqNo >= 0;

            AstNode node = null;
            while (true) {
                node = iterNodes.next();
                assert node != null;
                seqNo++;
                if (seqNo == q.seqNo) {
                    break;
                }
            }

            if (q.errCode != 0) {
                throw new SemanticError(node.lineNo(), q.errMsg); // s411
            }

            if (q instanceof ServerAPI.ProcedureSignature) {
                ServerAPI.ProcedureSignature ps = (ServerAPI.ProcedureSignature) q;

                NodeList<DeclParam> paramList = new NodeList<>();
                String err = makeParamList(paramList, ps.name, ps.params);
                if (err != null) {
                    throw new SemanticError( // s412
                            node.lineNo(), err);
                }

                assert node instanceof StmtGlobalProcCall;
                StmtGlobalProcCall gpc = (StmtGlobalProcCall) node;
                assert gpc.name.equals(ps.name);

                int errCode = checkArguments(gpc.args, paramList);
                if (errCode < 0) {
                    throw new SemanticError( // s413
                            node.lineNo(),
                            "number of the arguments to procedure "
                                    + ps.name
                                    + " does not match the number of the procedure's formal parameters");
                } else if (errCode > 0) {
                    throw new SemanticError( // s414
                            node.lineNo(),
                            "argument "
                                    + errCode
                                    + " to the call of "
                                    + ps.name
                                    + " must be assignable to because it is to an OUT parameter");
                }

                gpc.decl = new DeclProc(null, ps.name, paramList);

            } else if (q instanceof ServerAPI.FunctionSignature) {
                ServerAPI.FunctionSignature fs = (ServerAPI.FunctionSignature) q;

                NodeList<DeclParam> paramList = new NodeList<>();
                String err = makeParamList(paramList, fs.name, fs.params);
                if (err != null) {
                    throw new SemanticError( // s415
                            node.lineNo(), err);
                }

                assert node instanceof ExprGlobalFuncCall;
                ExprGlobalFuncCall gfc = (ExprGlobalFuncCall) node;
                assert gfc.name.equals(fs.name);

                int errCode = checkArguments(gfc.args, paramList);
                if (errCode < 0) {
                    throw new SemanticError( // s416
                            node.lineNo(),
                            "number of the arguments to function "
                                    + fs.name
                                    + " does not match the number of the function's formal parameters");
                } else if (errCode > 0) {
                    throw new SemanticError( // s417
                            node.lineNo(),
                            "argument "
                                    + errCode
                                    + " to the call of "
                                    + fs.name
                                    + " must be assignable to because it is to an OUT parameter");
                }

                String sqlType = getSqlTypeNameFromCode(fs.retType.type);
                String javaType = getJavaType(sqlType);
                if (javaType == null) {
                    throw new SemanticError( // s418
                            node.lineNo(),
                            "the function uses unsupported type "
                                    + sqlType
                                    + " as its return type");
                }
                TypeSpec retType = TypeSpecSimple.of(javaType);
                assert retType != null;

                gfc.decl = new DeclFunc(null, fs.name, paramList, retType);

            } else if (q instanceof ServerAPI.SerialOrNot) {

                assert node instanceof ExprSerialVal;
                ((ExprSerialVal) node).verified = true;
            } else if (q instanceof ServerAPI.ColumnType) {
                ServerAPI.ColumnType ct = (ServerAPI.ColumnType) q;

                String sqlType = getSqlTypeNameFromCode(ct.colType.type);
                String javaType = getJavaType(sqlType);
                if (javaType == null) {
                    throw new SemanticError( // s410
                            node.lineNo(),
                            "the table column "
                                    + ct.table
                                    + "."
                                    + ct.column
                                    + " has an unsupported type "
                                    + sqlType);
                }
                TypeSpec ty = TypeSpecSimple.of(javaType);
                assert ty != null;

                assert node instanceof TypeSpecPercent;
                ((TypeSpecPercent) node).resolvedType = ty;
            } else {
                assert false : "unreachable";
            }
        }
    }

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
        return new Unit(ctx, autonomousTransaction, connectionRequired, getImportString(), decl);
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
            ExprId id = visitNonFuncIdentifier(ctx.identifier()); // s000: undeclared id
            if (!(id.decl instanceof DeclIdTyped)) {
                throw new SemanticError(
                        Misc.getLineOf(ctx), // s001
                        id.name
                                + " may not use %TYPE because it is neither a parameter of a "
                                + "procedure/function, variable, nor constant");
            }

            return ((DeclIdTyped) id.decl).typeSpec();
        } else {
            // case table.column%TYPE
            String table = Misc.getNormalizedText(ctx.table_name());
            String column = Misc.getNormalizedText(ctx.identifier());

            TypeSpec ret = new TypeSpecPercent(table, column);
            semanticQuestions.put(ret, new ServerAPI.ColumnType(table, column));
            return ret;
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
            assert false; // by syntax
            throw new RuntimeException("unreachable");
        }

        // TODO: restore two lines below
        // addToImports("java.math.BigDecimal");
        // return new TypeSpecNumeric(precision, scale);
        return TypeSpecSimple.of(getJavaType("NUMERIC")); // ignore precision and scale for now
    }

    @Override
    public TypeSpecSimple visitChar_type(Char_typeContext ctx) {
        // ignore length for now
        return TypeSpecSimple.of("java.lang.String");
    }

    @Override
    public TypeSpecSimple visitSimple_type(Simple_typeContext ctx) {
        String pcsType = Misc.getNormalizedText(ctx);
        String javaType = getJavaType(pcsType);
        assert javaType != null : "no java type for PL/CSQL type " + pcsType;
        return TypeSpecSimple.of(javaType);
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
            assert false : "unreachable"; // by syntax
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
        Expr lowerBound = visitExpression(ctx.between_elements().between_expression(0));
        Expr upperBound = visitExpression(ctx.between_elements().between_expression(1));

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

        if (escape != null) {
            String escapeStr = StringEscapeUtils.unescapeJava(escape.val);
            if (escapeStr.length() != 1) {
                throw new SemanticError(
                        Misc.getLineOf(ctx.escape), // s002
                        "the escape does not consist of a single character");
            }
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
            assert false : "unreachable"; // by syntax
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
            assert false : "unreachable"; // by syntax
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
            assert false : "unreachable"; // by syntax
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
            assert false : "unreachable"; // by syntax
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
        s = unquoteStr(s);
        LocalDate date = DateTimeParser.DateLiteral.parse(s);
        if (date == null) {
            throw new SemanticError(
                    Misc.getLineOf(ctx), // s003
                    "invalid DATE string: " + s);
        }
        // System.out.println("[temp] date=" + date);
        return new ExprDate(ctx, date);
    }

    @Override
    public Expr visitTime_exp(Time_expContext ctx) {
        String s = ctx.quoted_string().getText();
        s = unquoteStr(s);
        LocalTime time = DateTimeParser.TimeLiteral.parse(s);
        if (time == null) {
            throw new SemanticError(
                    Misc.getLineOf(ctx), // s004
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
        s = unquoteStr(s);
        LocalDateTime datetime = DateTimeParser.DatetimeLiteral.parse(s);
        if (datetime == null) {
            throw new SemanticError(
                    Misc.getLineOf(ctx), // s005
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
            ExprUint.Type ty;

            BigInteger bi = new BigInteger(ctx.UNSIGNED_INTEGER().getText());
            if (bi.compareTo(UINT_LITERAL_MAX) > 0) {
                throw new SemanticError(
                        Misc.getLineOf(ctx), // s006
                        "number of digits of an integer literal may not exceed 38");
            } else if (bi.compareTo(BIGINT_MAX) > 0) {
                addToImports("java.math.BigDecimal");
                ty = ExprUint.Type.BIGDECIMAL;
            } else if (bi.compareTo(INT_MAX) > 0) {
                ty = ExprUint.Type.LONG;
            } else {
                ty = ExprUint.Type.INTEGER;
            }

            return new ExprUint(ctx, bi.toString(), ty);
        } catch (NumberFormatException e) {
            assert false : "unreachable"; // by syntax
            throw new RuntimeException("unreachable");
        }
    }

    @Override
    public ExprFloat visitFp_num_exp(Fp_num_expContext ctx) {
        try {
            BigDecimal bd = new BigDecimal(ctx.FLOATING_POINT_NUM().getText());
            return new ExprFloat(ctx, bd.toString());
        } catch (NumberFormatException e) {
            assert false : "unreachable"; // by syntax
            throw new RuntimeException("unreachable");
        }
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

        try {
            ExprId record = visitNonFuncIdentifier(ctx.record);
            if (!(record.decl instanceof DeclForRecord)) {
                throw new SemanticError(
                        Misc.getLineOf(ctx.record), // s008
                        "field lookup is only allowed for records");
            }

            Scope scope = symbolStack.getCurrentScope();
            return new ExprField(ctx, record, fieldName);

        } catch (SemanticError e) {

            String msg = e.getMessage();
            if (msg.startsWith("undeclared id")) {

                if (fieldName.equals("CURRENT_VALUE") || fieldName.equals("NEXT_VALUE")) {

                    connectionRequired = true;
                    addToImports("java.sql.*");

                    String recordText = Misc.getNormalizedText(ctx.record);
                    // do not push a symbol table: no nested structure
                    Expr ret =
                            new ExprSerialVal(
                                    ctx,
                                    recordText,
                                    fieldName.equals("CURRENT_VALUE")
                                            ? ExprSerialVal.SerialVal.CURR_VAL
                                            : ExprSerialVal.SerialVal.NEXT_VAL);
                    semanticQuestions.put(ret, new ServerAPI.SerialOrNot(recordText));
                    return ret;
                } else {
                    throw e; // s007: undeclared id ...
                }
            } else {
                throw e;
            }
        }
    }

    @Override
    public Expr visitFunction_call(Function_callContext ctx) {

        String name = Misc.getNormalizedText(ctx.identifier());
        NodeList<Expr> args = visitFunction_argument(ctx.function_argument());

        DeclFunc decl = symbolStack.getDeclFunc(name);
        if (decl == null) {

            connectionRequired = true;
            addToImports("java.sql.*");

            ExprGlobalFuncCall ret = new ExprGlobalFuncCall(ctx, name, args);
            semanticQuestions.put(ret, new ServerAPI.FunctionSignature(name));

            return ret;
        } else {
            if (decl.paramList.nodes.size() != args.nodes.size()) {
                throw new SemanticError(
                        Misc.getLineOf(ctx), // s009
                        "the number of arguments to function "
                                + name
                                + " does not match the number of its formal parameters");
            }

            int i = 0;
            for (Expr arg : args.nodes) {
                DeclParam dp = decl.paramList.nodes.get(i);

                if (dp instanceof DeclParamOut) {
                    if (arg instanceof ExprId && isAssignableTo((ExprId) arg)) {
                        // OK
                    } else {
                        throw new SemanticError(
                                Misc.getLineOf(arg.ctx), // s010
                                "argument "
                                        + i
                                        + " to the function "
                                        + name
                                        + " must be assignable to because it is to an OUT parameter");
                    }
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
            elsePart = null;
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
            elsePart = null;
        } else {
            elsePart = visitExpression(ctx.case_expression_else_part().expression());
        }

        symbolStack.popSymbolTable();

        return new ExprCase(ctx, selector, whenParts, elsePart);
    }

    @Override
    public AstNode visitCursor_attr_exp(Cursor_attr_expContext ctx) {

        ExprId cursor =
                visitNonFuncIdentifier(ctx.cursor_exp().identifier()); // s011: undeclared id ...
        if (!isCursorOrRefcursor(cursor)) {
            throw new SemanticError(
                    Misc.getLineOf(ctx), // s012
                    "cursor attributes may not be read from "
                            + cursor.name
                            + " which is neither a cursor nor a cursor reference");
        }

        ExprCursorAttr.Attr attr =
                ctx.PERCENT_ISOPEN() != null
                        ? ExprCursorAttr.Attr.ISOPEN
                        : ctx.PERCENT_FOUND() != null
                                ? ExprCursorAttr.Attr.FOUND
                                : ctx.PERCENT_NOTFOUND() != null
                                        ? ExprCursorAttr.Attr.NOTFOUND
                                        : ctx.PERCENT_ROWCOUNT() != null
                                                ? ExprCursorAttr.Attr.ROWCOUNT
                                                : null;
        assert attr != null; // by syntax

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
        assert ctx.AUTONOMOUS_TRANSACTION() != null; // by syntax

        // currently, only the Autonomous Transaction is
        // allowed only in the top-level declarations
        if (symbolStack.getCurrentScope().level != 2) {
            throw new SemanticError(
                    Misc.getLineOf(ctx), // s013
                    "AUTONOMOUS_TRANSACTION can only be declared at the top level");
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

        DeclVar ret = new DeclVar(ctx, name, ty, ctx.NOT() != null, val);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    @Override
    public AstNode visitCursor_definition(Cursor_definitionContext ctx) {

        String name = Misc.getNormalizedText(ctx.identifier());

        symbolStack.pushSymbolTable("cursor_def", null);

        NodeList<DeclParam> paramList = visitParameter_list(ctx.parameter_list());
        for (DeclParam dp : paramList.nodes) {
            if (dp instanceof DeclParamOut) {
                throw new SemanticError(
                        Misc.getLineOf(dp.ctx), // s014
                        "parameters of a cursor definition may not be OUT parameters");
            }
        }

        SqlSemantics sws = staticSqls.get(ctx.s_select_statement());
        assert sws != null;
        assert sws.kind == ServerConstants.CUBRID_STMT_SELECT; // by syntax
        if (sws.intoVars != null) {
            throw new SemanticError(
                    Misc.getLineOf(ctx.s_select_statement()), // s015
                    "SQL in a cursor definition may not have an into-clause");
        }
        StaticSql staticSql = checkAndConvertStaticSql(sws, ctx.s_select_statement());

        symbolStack.popSymbolTable();

        DeclCursor ret = new DeclCursor(ctx, name, paramList, staticSql);
        symbolStack.putDecl(name, ret);

        return ret;
    }

    @Override
    public DeclRoutine visitRoutine_definition(Routine_definitionContext ctx) {

        String name = Misc.getNormalizedText(ctx.identifier());

        boolean isFunction = (ctx.PROCEDURE() == null);

        symbolStack.pushSymbolTable(
                name, isFunction ? Misc.RoutineType.FUNC : Misc.RoutineType.PROC);

        visitParameter_list(ctx.parameter_list()); // just to put symbols to the symbol table

        NodeList<Decl> decls = visitSeq_of_declare_specs(ctx.seq_of_declare_specs());
        Body body = visitBody(ctx.body());
        if (body.label != null && !body.label.equals(name)) {
            throw new SemanticError(
                    Misc.getLineOf(ctx.body().label_name()),
                    String.format(
                            "label does not match the %s name %s",
                            isFunction ? "function" : "procedure", name)); // s053
        }

        symbolStack.popSymbolTable();

        DeclRoutine ret;
        if (isFunction) {
            ret = symbolStack.getDeclFunc(name);
            if (!controlFlowBlocked) {
                throw new SemanticError(
                        Misc.getLineOf(ctx), // s016
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

        controlFlowBlocked = allFlowsBlocked; // s017-1

        String label;
        if (ctx.label_name() == null) {
            label = null;
        } else {
            label = Misc.getNormalizedText(ctx.label_name());
        }

        return new Body(ctx, stmts, exHandlers, label);
    }

    @Override
    public NodeList<Stmt> visitSeq_of_statements(Seq_of_statementsContext ctx) {

        controlFlowBlocked = false;

        NodeList<Stmt> stmts = new NodeList<>();
        for (StatementContext sc : ctx.statement()) {
            if (controlFlowBlocked) {
                throw new SemanticError(
                        Misc.getLineOf(sc), // s017
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

        ExprId var = visitNonFuncIdentifier(ctx.identifier()); // s018: undeclared id ...
        if (!isAssignableTo(var)) {
            throw new SemanticError(
                    Misc.getLineOf(ctx.identifier()), // s019
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

            Expr ret = new ExprGlobalFuncCall(ctx, name, EMPTY_ARGS);
            semanticQuestions.put(ret, new ServerAPI.FunctionSignature(name));
            return ret;
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
    public Stmt visitContinue_statement(Continue_statementContext ctx) {

        if (!within(ctx, Loop_statementContext.class)) {
            throw new SemanticError(
                    Misc.getLineOf(ctx), // s020
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
                throw new SemanticError(
                        Misc.getLineOf(lnc), // s021
                        "undeclared label " + label);
            }
        }

        if (ctx.expression() == null) {
            controlFlowBlocked = true; // s017-2
            return new StmtContinue(ctx, declLabel);
        } else {
            Expr cond = visitExpression(ctx.expression());
            CondStmt cs = new CondStmt(ctx, cond, new StmtContinue(ctx, declLabel));
            return new StmtIf(ctx, true, new NodeList<CondStmt>().addNode(cs), null);
        }
    }

    @Override
    public Stmt visitExit_statement(Exit_statementContext ctx) {

        if (!within(ctx, Loop_statementContext.class)) {
            throw new SemanticError(
                    Misc.getLineOf(ctx), // s022
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
                throw new SemanticError(
                        Misc.getLineOf(lnc), // s023
                        "undeclared label " + label);
            }
        }

        if (ctx.expression() == null) {
            controlFlowBlocked = true; // s107-3
            return new StmtBreak(ctx, declLabel);
        } else {
            Expr cond = visitExpression(ctx.expression());
            CondStmt cs = new CondStmt(ctx, cond, new StmtBreak(ctx, declLabel));
            return new StmtIf(ctx, true, new NodeList<CondStmt>().addNode(cs), null);
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

        controlFlowBlocked = allFlowsBlocked; // s017-3

        return new StmtIf(ctx, true, condParts, elsePart);
    }

    @Override
    public StmtBasicLoop visitStmt_basic_loop(Stmt_basic_loopContext ctx) {

        symbolStack.pushSymbolTable("loop", null);

        DeclLabel declLabel = visitLabel_declaration(ctx.label_declaration());
        if (declLabel != null) {
            symbolStack.putDecl(declLabel.name, declLabel);
        }

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        controlFlowBlocked =
                false; // every loop is assumed not to block control flow in generated Java code

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

        Expr cond = visitExpression(ctx.expression());
        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        controlFlowBlocked =
                false; // every loop is assumed not to block control flow in generated Java code

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
        controlFlowBlocked =
                false; // every loop is assumed not to block control flow in generated Java code

        symbolStack.popSymbolTable();

        return new StmtForIterLoop(
                ctx, declLabel, iterDecl, reverse, lowerBound, upperBound, step, stmts);
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
        ExprId cursor = visitNonFuncIdentifier(idCtx); // s024: undeclared id ...
        if (!(cursor.decl instanceof DeclCursor)) {
            throw new SemanticError(
                    Misc.getLineOf(idCtx), // s025
                    Misc.getNormalizedText(idCtx) + " is not a cursor");
        }
        DeclCursor declCursor = (DeclCursor) cursor.decl;

        NodeList<Expr> args = visitExpressions(ctx.for_cursor().expressions());

        if (declCursor.paramList.nodes.size() != args.nodes.size()) {
            throw new SemanticError(
                    Misc.getLineOf(idCtx), // s026
                    "the number of arguments to cursor "
                            + Misc.getNormalizedText(idCtx)
                            + " does not match the number of its declared formal parameters");
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

        DeclForRecord declForRecord =
                new DeclForRecord(
                        ctx.for_cursor().record_name(), record, declCursor.staticSql.selectList);
        symbolStack.putDecl(record, declForRecord);

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        controlFlowBlocked =
                false; // every loop is assumed not to block control flow in generated Java code

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

        SqlSemantics sws = staticSqls.get(selectCtx);
        assert sws != null;
        assert sws.kind == ServerConstants.CUBRID_STMT_SELECT; // by syntax
        if (sws.intoVars != null) {
            throw new SemanticError(
                    Misc.getLineOf(selectCtx), // s027
                    "SELECT in a FOR loop may not have an into-clause");
        }
        StaticSql staticSql = checkAndConvertStaticSql(sws, selectCtx);

        String label;
        DeclLabel declLabel = visitLabel_declaration(ctx.label_declaration());
        if (declLabel == null) {
            label = null;
        } else {
            label = declLabel.name;
            symbolStack.putDecl(label, declLabel);
        }

        DeclForRecord declForRecord = new DeclForRecord(recNameCtx, record, staticSql.selectList);
        symbolStack.putDecl(record, declForRecord);

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        controlFlowBlocked =
                false; // every loop is assumed not to block control flow in generated Java code

        symbolStack.popSymbolTable();

        return new StmtForStaticSqlLoop(ctx, label, declForRecord, staticSql, stmts);
    }

    @Override
    public StmtForSqlLoop visitStmt_for_dynamic_sql_loop(Stmt_for_dynamic_sql_loopContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        symbolStack.pushSymbolTable("for_d_sql_loop", null);

        ParserRuleContext recNameCtx = ctx.for_dynamic_sql().record_name();
        String record = Misc.getNormalizedText(recNameCtx);

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

        DeclForRecord declForRecord =
                new DeclForRecord(recNameCtx, record, null); // null: unknown field types
        symbolStack.putDecl(record, declForRecord);

        NodeList<Stmt> stmts = visitSeq_of_statements(ctx.seq_of_statements());
        controlFlowBlocked =
                false; // every loop is assumed not to block control flow in generated Java code

        symbolStack.popSymbolTable();

        return new StmtForExecImmeLoop(ctx, label, declForRecord, dynSql, usedExprList, stmts);
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
                throw new SemanticError(
                        Misc.getLineOf(ctx), // s028
                        "raise statements without an exception name can only be in an exception handler");
            }
        }

        controlFlowBlocked = true; // s017-4
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
            throw new SemanticError(
                    Misc.getLineOf(ctx), // s029
                    "undeclared exception " + name);
        }

        Scope scope = symbolStack.getCurrentScope();

        return new ExName(ctx, name, scope, decl);
    }

    @Override
    public StmtReturn visitReturn_statement(Return_statementContext ctx) {

        controlFlowBlocked = true; // s017-5

        Misc.RoutineType routineType = symbolStack.getCurrentScope().routineType;
        if (ctx.expression() == null) {
            if (routineType != Misc.RoutineType.PROC) {
                throw new SemanticError(
                        Misc.getLineOf(ctx), // s030
                        "function "
                                + symbolStack.getCurrentScope().routine
                                + " must return a value");
            }
            return new StmtReturn(ctx, null, null);
        } else {
            if (routineType != Misc.RoutineType.FUNC) {
                throw new SemanticError(
                        Misc.getLineOf(ctx), // s031
                        "procedure "
                                + symbolStack.getCurrentScope().routine
                                + " may not return a value");
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
            elsePart = null;
            // allFlowsBlocked = allFlowsBlocked && true;
        } else {
            elsePart = visitSeq_of_statements(ctx.case_statement_else_part().seq_of_statements());
            allFlowsBlocked = allFlowsBlocked && controlFlowBlocked;
        }

        symbolStack.popSymbolTable();

        if (whenParts.nodes.size() > 0) {
            addToImports("java.util.Objects");
        }

        controlFlowBlocked = allFlowsBlocked; // s017-6
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
            elsePart = null;
            // allFlowsBlocked = allFlowsBlocked && true;
        } else {
            elsePart = visitSeq_of_statements(ctx.case_statement_else_part().seq_of_statements());
            allFlowsBlocked = allFlowsBlocked && controlFlowBlocked;
        }

        controlFlowBlocked = allFlowsBlocked; // s017-7
        return new StmtIf(ctx, false, condParts, elsePart);
    }

    @Override
    public StmtRaiseAppErr visitRaise_application_error_statement(
            Raise_application_error_statementContext ctx) {

        Expr errCode = visitExpression(ctx.err_code());
        Expr errMsg = visitExpression(ctx.err_msg());

        controlFlowBlocked = true; // s017-8
        return new StmtRaiseAppErr(ctx, errCode, errMsg);
    }

    @Override
    public StmtStaticSql visitData_manipulation_language_statements(
            Data_manipulation_language_statementsContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        SqlSemantics sws = staticSqls.get(ctx);
        assert sws != null;
        StaticSql staticSql = checkAndConvertStaticSql(sws, ctx);
        if (staticSql.kind == ServerConstants.CUBRID_STMT_SELECT) {
            if (staticSql.intoVars == null) {
                throw new SemanticError(
                        Misc.getLineOf(ctx), // s055
                        "SELECT statement must have an into-clause");
            }
            for (ExprId var : staticSql.intoVars) {
                if (!isAssignableTo(var)) {
                    throw new SemanticError(
                            Misc.getLineOf(ctx), // s056
                            "variable " + var.name + " in an into-clause must be assignable to");
                }
            }
        }

        int level = symbolStack.getCurrentScope().level + 1;
        return new StmtStaticSql(ctx, level, staticSql);
    }

    @Override
    public AstNode visitClose_statement(Close_statementContext ctx) {

        IdentifierContext idCtx = ctx.cursor_exp().identifier();

        ExprId cursor = visitNonFuncIdentifier(idCtx); // s032: undeclared id ...
        if (!isCursorOrRefcursor(cursor)) {
            throw new SemanticError(
                    Misc.getLineOf(idCtx), // s033
                    cursor.name
                            + " may not be closed because it is neither a cursor nor a cursor reference");
        }

        return new StmtCursorClose(ctx, cursor);
    }

    @Override
    public AstNode visitOpen_statement(Open_statementContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        IdentifierContext idCtx = ctx.cursor_exp().identifier();

        ExprId cursor = visitNonFuncIdentifier(idCtx); // s034: undeclared id ...
        if (!(cursor.decl instanceof DeclCursor)) {
            throw new SemanticError(
                    Misc.getLineOf(idCtx), // s035
                    cursor.name + " may not be opened because it is not a cursor");
        }
        DeclCursor decl = (DeclCursor) cursor.decl;

        NodeList<Expr> args = visitExpressions(ctx.expressions());

        if (decl.paramList.nodes.size() != args.nodes.size()) {
            throw new SemanticError(
                    Misc.getLineOf(ctx.expressions()), // s036
                    "the number of arguments to cursor "
                            + Misc.getNormalizedText(idCtx)
                            + " does not match the number of its declared formal parameters");
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
        ExprId cursor = visitNonFuncIdentifier(idCtx); // s037: undeclared id ...
        if (!isCursorOrRefcursor(cursor)) {
            throw new SemanticError(
                    Misc.getLineOf(idCtx), // s038
                    cursor.name
                            + " may not be fetched becaused it is neither a cursor nor a cursor reference");
        }

        NodeList<ExprId> intoVars = new NodeList<>();
        for (IdentifierContext v : ctx.identifier()) {
            ExprId id = visitNonFuncIdentifier(v); // s060: undeclared id ...
            if (!isAssignableTo(id)) {
                throw new SemanticError(
                        Misc.getLineOf(v), // s039
                        "variables to store fetch results must be assignable to");
            }
            intoVars.addNode(id);
        }

        if (cursor.decl instanceof DeclCursor) {
            if (intoVars.nodes.size() != ((DeclCursor) cursor.decl).staticSql.selectList.size()) {
                throw new SemanticError( // TODO: verify what happens in Oracle
                        cursor.lineNo(), // s059
                        "the number of columns of the cursor must be equal to the number of into-variables");
            }
        }

        return new StmtCursorFetch(ctx, cursor, intoVars);
    }

    @Override
    public AstNode visitOpen_for_statement(Open_for_statementContext ctx) {

        connectionRequired = true;
        addToImports("java.sql.*");

        ExprId refCursor = visitNonFuncIdentifier(ctx.identifier()); // s040: undeclared id ...
        if (!isAssignableTo(refCursor)) {
            throw new SemanticError(
                    Misc.getLineOf(ctx.identifier()), // s041
                    "identifier in an OPEN-FOR statement must be assignable to");
        }
        if (!((DeclIdTyped) refCursor.decl).typeSpec().equals(TypeSpecSimple.REFCURSOR)) {
            throw new SemanticError(
                    Misc.getLineOf(ctx.identifier()), // s042
                    "identifier in an OPEN-FOR statement must be of SYS_REFCURSOR type");
        }

        SqlSemantics sws = staticSqls.get(ctx.s_select_statement());
        assert sws != null;
        assert sws.kind == ServerConstants.CUBRID_STMT_SELECT; // by syntax
        if (sws.intoVars != null) {
            throw new SemanticError(
                    Misc.getLineOf(ctx.s_select_statement()), // s043
                    "SQL in an OPEN-FOR statement may not have an into-clause");
        }
        StaticSql staticSql = checkAndConvertStaticSql(sws, ctx.s_select_statement());

        return new StmtOpenFor(ctx, refCursor, staticSql);
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

            connectionRequired = true;
            addToImports("java.sql.*");

            int level = symbolStack.getCurrentScope().level + 1;
            StmtGlobalProcCall ret = new StmtGlobalProcCall(ctx, level, name, args);
            semanticQuestions.put(ret, new ServerAPI.ProcedureSignature(name));

            return ret;
        } else {
            if (decl.paramList.nodes.size() != args.nodes.size()) {
                throw new SemanticError(
                        Misc.getLineOf(ctx), // s044
                        "the number of arguments to procedure "
                                + name
                                + " does not match the number of its formal parameters");
            }

            int i = 0;
            for (Expr arg : args.nodes) {
                DeclParam dp = decl.paramList.nodes.get(i);

                if (dp instanceof DeclParamOut) {
                    if (arg instanceof ExprId && isAssignableTo((ExprId) arg)) {
                        // OK
                    } else {
                        throw new SemanticError(
                                Misc.getLineOf(arg.ctx), // s045
                                "argument "
                                        + (i + 1)
                                        + " to the procedure "
                                        + name
                                        + " must be assignable to because it is to an OUT parameter");
                    }
                }

                i++;
            }

            return new StmtLocalProcCall(ctx, name, args, symbolStack.getCurrentScope(), decl);
        }
    }

    @Override
    public StmtSql visitExecute_immediate(Execute_immediateContext ctx) {

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
        return new StmtExecImme(ctx, level, dynSql, intoVarList, usedExprList);
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
                    throw new SemanticError(
                            Misc.getLineOf(c), // s046
                            "expression '"
                                    + c.expression().getText()
                                    + "' may not be used there in the USING clause because it is not assignable to");
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
            ExprId id = visitNonFuncIdentifier(c); // s047: undeclared id ...
            if (!isAssignableTo(id)) {
                throw new SemanticError(
                        Misc.getLineOf(c), // s048
                        "variable "
                                + Misc.getNormalizedText(c)
                                + " may not be used there in the INTO clause because it is not assignable to");
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
            throw new SemanticError(
                    Misc.getLineOf(others), // s049
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
    // Private Static
    // --------------------------------------------------------

    private static final BigInteger UINT_LITERAL_MAX =
            new BigInteger("99999999999999999999999999999999999999");
    private static final BigInteger BIGINT_MAX = new BigInteger("9223372036854775807");
    private static final BigInteger INT_MAX = new BigInteger("2147483648");

    private static final String SYMBOL_TABLE_TOP = "%predefined";
    private static final NodeList<DeclParam> EMPTY_PARAMS = new NodeList<>();
    private static final NodeList<Expr> EMPTY_ARGS = new NodeList<>();

    private static boolean isCursorOrRefcursor(ExprId id) {

        DeclId decl = id.decl;
        return (decl instanceof DeclCursor
                || ((decl instanceof DeclVar || decl instanceof DeclParam)
                        && ((DeclIdTyped) decl).typeSpec().equals(TypeSpecSimple.REFCURSOR)));
    }

    private static final Map<String, String> pcsToJavaTypeMap = new TreeMap<>();

    static {
        pcsToJavaTypeMap.put("BOOLEAN", "java.lang.Boolean");

        pcsToJavaTypeMap.put("CHAR", "java.lang.String");
        pcsToJavaTypeMap.put("CHARACTER", "java.lang.String");
        pcsToJavaTypeMap.put("VARCHAR", "java.lang.String");
        pcsToJavaTypeMap.put("CHAR VARYING", "java.lang.String");
        pcsToJavaTypeMap.put("CHARACTER VARYING", "java.lang.String");
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

    private static String unquoteStr(String val) {
        val = val.substring(1, val.length() - 1); // strip enclosing '
        return val.replace("''", "'");
    }

    private static String quotedStrToJavaStr(String val) {
        return StringEscapeUtils.escapeJava(unquoteStr(val));
    }

    // --------------------------------------------------------
    // Private
    // --------------------------------------------------------

    private final LinkedHashMap<AstNode, ServerAPI.Question> semanticQuestions =
            new LinkedHashMap<>();

    private final Map<ParserRuleContext, SqlSemantics> staticSqls;
    private final Set<String> imports = new TreeSet<>();

    private boolean autonomousTransaction = false;
    private boolean connectionRequired = false;

    private boolean controlFlowBlocked;

    private ExprId visitNonFuncIdentifier(IdentifierContext ctx) {
        return visitNonFuncIdentifier(Misc.getNormalizedText(ctx), ctx);
    }

    private ExprId visitNonFuncIdentifier(String name, ParserRuleContext ctx) {
        ExprId ret;
        Decl decl = symbolStack.getDeclForIdExpr(name);
        if (decl == null) {
            ret = null; // no such id at all
        } else if (decl instanceof DeclId) {
            Scope scope = symbolStack.getCurrentScope();
            return new ExprId(ctx, name, scope, (DeclId) decl);
        } else if (decl instanceof DeclFunc) {
            ret = null; // the name represents a function in its scope
        } else {
            assert false : "unreachable";
            throw new RuntimeException("unreachable");
        }

        if (ret == null) {
            throw new SemanticError(Misc.getLineOf(ctx), "undeclared id " + name);
        } else {
            return ret;
        }
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
                throw new SemanticError(
                        Misc.getLineOf(ctx), // s050
                        "function " + name + " must specify its return type");
            }
            TypeSpec retType = (TypeSpec) visit(ctx.type_spec());
            DeclFunc ret = new DeclFunc(ctx, name, paramList, retType);
            symbolStack.putDecl(name, ret);
        } else {
            // procedure
            if (ctx.RETURN() != null) {
                throw new SemanticError(
                        Misc.getLineOf(ctx), // s051
                        "procedure " + name + " may not specify a return type");
            }
            DeclProc ret = new DeclProc(ctx, name, paramList);
            symbolStack.putDecl(name, ret);
        }
    }

    private void addToImports(String i) {
        // System.out.println("temp: i = " + i);
        imports.add(i);
    }

    private String getJavaType(String pcsType) {
        String javaType = pcsToJavaTypeMap.get(pcsType);
        if (javaType == null) {
            return null;
        }

        if ("com.cubrid.plcsql.predefined.sp.SpLib.Query".equals(javaType)) {
            // no need to import Cursor now
        } else if (javaType.startsWith("java.lang.")
                && javaType.lastIndexOf('.') == 9) { // 9:the index of the second '.'
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

    private ExprZonedDateTime parseZonedDateTime(
            ParserRuleContext ctx, String s, boolean forDatetime, String originType) {

        s = unquoteStr(s);
        ZonedDateTime timestamp = DateTimeParser.ZonedDateTimeLiteral.parse(s, forDatetime);
        if (timestamp == null) {
            throw new SemanticError(
                    Misc.getLineOf(ctx), // s052
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

    private String getSqlTypeNameFromCode(int sqlTypeCode) {
        String ret = null;
        switch (sqlTypeCode) { // got from com.cubrid.jsp.data.DBType
            case DBType.DB_NULL:
                ret = "NULL";
                break;
            case DBType.DB_INT:
                ret = "INT";
                break;
            case DBType.DB_FLOAT:
                ret = "FLOAT";
                break;
            case DBType.DB_DOUBLE:
                ret = "DOUBLE";
                break;
            case DBType.DB_STRING:
                ret = "STRING";
                break;
            case DBType.DB_OBJECT:
                ret = "OBJECT";
                break;
            case DBType.DB_SET:
                ret = "SET";
                break;
            case DBType.DB_MULTISET:
                ret = "MULTISET";
                break;
            case DBType.DB_SEQUENCE:
                ret = "SEQUENCE";
                break;
            case DBType.DB_TIME:
                ret = "TIME";
                break;
            case DBType.DB_TIMESTAMP:
                ret = "TIMESTAMP";
                break;
            case DBType.DB_DATE:
                ret = "DATE";
                break;
            case DBType.DB_MONETARY:
                ret = "MONETARY";
                break;
            case DBType.DB_SHORT:
                ret = "SHORT";
                break;
            case DBType.DB_NUMERIC:
                ret = "NUMERIC";
                break;
            case DBType.DB_OID:
                ret = "OID";
                break;
            case DBType.DB_CHAR:
                ret = "CHAR";
                break;
            case DBType.DB_RESULTSET:
                ret = "RESULTSET";
                break;
            case DBType.DB_BIGINT:
                ret = "BIGINT";
                break;
            case DBType.DB_DATETIME:
                ret = "DATETIME";
                break;

            case DBType.DB_BIT:
                ret = "BIT";
                break;
            case DBType.DB_VARBIT:
                ret = "VARBIT";
                break;

            case DBType.DB_BLOB:
                ret = "BLOB";
                break;
            case DBType.DB_CLOB:
                ret = "CLOB";
                break;
            case DBType.DB_ENUMERATION:
                ret = "ENUMERATION";
                break;
            case DBType.DB_TIMESTAMPTZ:
                ret = "TIMESTAMPTZ";
                break;
            case DBType.DB_TIMESTAMPLTZ:
                ret = "TIMESTAMPLTZ";
                break;
            case DBType.DB_DATETIMETZ:
                ret = "DATETIMETZ";
                break;
            case DBType.DB_DATETIMELTZ:
                ret = "DATETIMELTZ";
                break;
            default:
                assert false : "unreachable";
                throw new RuntimeException("unreachable");
        }

        return ret;
    }

    private StaticSql checkAndConvertStaticSql(SqlSemantics sws, ParserRuleContext ctx) {

        LinkedHashMap<ExprId, TypeSpec> hostVars = new LinkedHashMap<>();
        LinkedHashMap<String, TypeSpec> selectList = null;
        ArrayList<ExprId> intoVars = null;

        // check (name-binding) and convert host variables used in the SQL
        if (sws.hostVars != null) {
            for (PlParamInfo pi : sws.hostVars) {
                String sqlType = getSqlTypeNameFromCode(pi.type);

                String var = Misc.getNormalizedText(pi.name);
                ExprId id = visitNonFuncIdentifier(var, ctx); // s408: undeclared id ...

                TypeSpec javaType = TypeSpec.of(pcsToJavaTypeMap.get(sqlType));
                assert javaType != null;
                hostVars.put(id, javaType);
            }
        }

        if (sws.kind == ServerConstants.CUBRID_STMT_SELECT) {

            // convert select list
            selectList = new LinkedHashMap<>();
            for (ColumnInfo ci : sws.selectList) {
                String sqlType = getSqlTypeNameFromCode(ci.type);

                String col = Misc.getNormalizedText(ci.colName);
                TypeSpec javaType = TypeSpec.of(pcsToJavaTypeMap.get(sqlType));
                assert javaType != null;
                selectList.put(col, javaType);
            }

            // check (name-binding) and convert into-variables used in the SQL
            if (sws.intoVars != null) {

                if (sws.intoVars.size() != sws.selectList.size()) {
                    throw new SemanticError(
                            Misc.getLineOf(ctx), // s402
                            "the length of select list is different from the length of into-variables");
                }

                intoVars = new ArrayList<>();
                for (String var : sws.intoVars) {
                    var = Misc.getNormalizedText(var);
                    ExprId id = visitNonFuncIdentifier(var, ctx); // s409: undeclared id ...
                    intoVars.add(id);
                }
            }
        }

        String rewritten = StringEscapeUtils.escapeJava(sws.rewritten);
        return new StaticSql(ctx, sws.kind, rewritten, hostVars, selectList, intoVars);
    }

    private String makeParamList(NodeList<DeclParam> paramList, String name, PlParamInfo[] params) {

        int len = params.length;
        for (int i = 0; i < len; i++) {

            String sqlType = getSqlTypeNameFromCode(params[i].type);
            String javaType = getJavaType(sqlType);
            if (javaType == null) {
                return name + " uses unsupported type " + sqlType + " for parameter " + (i + 1);
            }
            TypeSpec paramType = TypeSpecSimple.of(javaType);
            assert paramType != null;

            if ((params[i].mode & ServerConstants.PARAM_MODE_OUT) > 0) {
                paramList.nodes.add(new DeclParamOut(null, "p" + i, paramType));
            } else {
                paramList.nodes.add(new DeclParamIn(null, "p" + i, paramType));
            }
        }

        return null;
    }

    private int checkArguments(NodeList<Expr> args, NodeList<DeclParam> params) {

        if (params.nodes.size() != args.nodes.size()) {
            return -1;
        }

        int i = 0;
        for (Expr arg : args.nodes) {
            DeclParam dp = params.nodes.get(i);

            if (dp instanceof DeclParamOut) {
                if (arg instanceof ExprId && isAssignableTo((ExprId) arg)) {
                    // OK
                } else {
                    return (i + 1);
                }
            }

            i++;
        }

        return 0;
    }
}
