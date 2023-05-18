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

package com.cubrid.plcsql.compiler.visitor;

import com.cubrid.plcsql.compiler.Coercion;
import com.cubrid.plcsql.compiler.CoercionScheme;
import com.cubrid.plcsql.compiler.Misc;
import com.cubrid.plcsql.compiler.SemanticError;
import com.cubrid.plcsql.compiler.StaticSql;
import com.cubrid.plcsql.compiler.SymbolStack;
import com.cubrid.plcsql.compiler.ast.*;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;

public class TypeChecker extends AstVisitor<TypeSpec> {

    public TypeChecker(SymbolStack symbolStack) {
        this.symbolStack = symbolStack;
    }

    @Override
    public TypeSpec visitUnit(Unit node) {
        visit(node.routine);
        return null;
    }

    @Override
    public TypeSpec visitBody(Body node) {
        visitNodeList(node.stmts);
        visitNodeList(node.exHandlers);
        return null;
    }

    @Override
    public TypeSpec visitExHandler(ExHandler node) {
        visitNodeList(node.stmts);
        return null;
    }

    /* TODO: restore
    @Override
    public TypeSpec visitTypeSpecNumeric(TypeSpecNumeric node) {
        return null; // nothing to do
    }
     */

    @Override
    public TypeSpec visitTypeSpecPercent(TypeSpecPercent node) {
        assert node.resolvedType != null;
        return null;
    }

    @Override
    public TypeSpec visitTypeSpecSimple(TypeSpecSimple node) {
        return null; // nothing to do
    }

    @Override
    public TypeSpec visitCaseExpr(CaseExpr node) {
        TypeSpec caseValType = visit(node.val);
        assert caseValTypes != null;
        caseValTypes.add(caseValType);
        return visit(node.expr);
    }

    @Override
    public TypeSpec visitCaseStmt(CaseStmt node) {
        TypeSpec caseValType = visit(node.val);
        assert caseValTypes != null;
        caseValTypes.add(caseValType);
        visitNodeList(node.stmts);
        return null;
    }

    @Override
    public TypeSpec visitCondExpr(CondExpr node) {
        TypeSpec caseCondType = visit(node.cond);
        if (!caseCondType.equals(TypeSpecSimple.BOOLEAN)) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.cond.ctx), // s202
                    "the condition must be boolean");
        }
        return visit(node.expr);
    }

    @Override
    public TypeSpec visitCondStmt(CondStmt node) {
        TypeSpec caseCondType = visit(node.cond);
        if (!caseCondType.equals(TypeSpecSimple.BOOLEAN)) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.cond.ctx), // s203
                    "type of the condition must be boolean");
        }
        visitNodeList(node.stmts);
        return null;
    }

    @Override
    public TypeSpec visitDeclParamIn(DeclParamIn node) {
        visitDeclParam(node);
        return null;
    }

    @Override
    public TypeSpec visitDeclParamOut(DeclParamOut node) {
        visitDeclParam(node);
        return null;
    }

    @Override
    public TypeSpec visitDeclVar(DeclVar node) {
        visit(node.typeSpec);
        if (node.val == null) {
            assert !node.notNull; // syntactically guaranteed
        } else {
            TypeSpec valType = visit(node.val);
            if (node.notNull && valType.equals(TypeSpecSimple.NULL)) {
                throw new SemanticError(
                        Misc.getLineColumnOf(node.val.ctx), // s204
                        "NOT NULL variables may not have null as their initial value");
            }

            Coercion c = Coercion.getCoercion(valType, node.typeSpec);
            if (c == null) {
                throw new SemanticError(
                        Misc.getLineColumnOf(node.val.ctx), // s205
                        "type of the initial value is not compatible with the variable's declared type");
            } else {
                node.val.setCoercion(c);
            }
        }

        return null;
    }

    @Override
    public TypeSpec visitDeclConst(DeclConst node) {
        visit(node.typeSpec);
        assert node.val != null; // syntactically guaranteed
        TypeSpec valType = visit(node.val);
        if (node.notNull && valType.equals(TypeSpecSimple.NULL)) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.val.ctx), // s206
                    "NOT NULL constants may not have null as their initial value");
        }

        Coercion c = Coercion.getCoercion(valType, node.typeSpec);
        if (c == null) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.val.ctx), // s207
                    "type of the initial value is not compatible with the constant's declared type");
        } else {
            node.val.setCoercion(c);
        }

        return null;
    }

    @Override
    public TypeSpec visitDeclFunc(DeclFunc node) {
        return visitDeclRoutine(node);
    }

    @Override
    public TypeSpec visitDeclProc(DeclProc node) {
        return visitDeclRoutine(node);
    }

    @Override
    public TypeSpec visitDeclCursor(DeclCursor node) {

        assert node.staticSql.intoVars == null; // by earlier check

        visitNodeList(node.paramList);
        typeCheckHostExprs(node.staticSql); // s400
        return null;
    }

    @Override
    public TypeSpec visitDeclLabel(DeclLabel node) {
        return null; // nothing to do
    }

    @Override
    public TypeSpec visitDeclException(DeclException node) {
        return null; // nothing to do
    }

    @Override
    public TypeSpec visitExprBetween(ExprBetween node) {
        TypeSpec targetType = visit(node.target);
        TypeSpec lowerType = visit(node.lowerBound);
        TypeSpec upperType = visit(node.upperBound);

        List<Coercion> outCoercions = new ArrayList<>();
        DeclFunc op =
                symbolStack.getOperator(
                        outCoercions, "opBetween", targetType, lowerType, upperType);
        if (op == null) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.ctx), // s208, s209
                    "lower bound or upper bound does not have a comparable type");
        }
        assert outCoercions.size() == 3;

        node.target.setCoercion(outCoercions.get(0));
        node.lowerBound.setCoercion(outCoercions.get(1));
        node.upperBound.setCoercion(outCoercions.get(2));

        return TypeSpecSimple.BOOLEAN;
    }

    @Override
    public TypeSpec visitExprBinaryOp(ExprBinaryOp node) {
        TypeSpec leftType = visit(node.left);
        TypeSpec rightType = visit(node.right);

        // in the following line, s210 (no match)
        List<Coercion> outCoercions = new ArrayList<>();
        DeclFunc binOp =
                symbolStack.getOperator(outCoercions, "op" + node.opStr, leftType, rightType);
        if (binOp == null) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.ctx), // s210
                    "operands do not have compatible types");
        }
        assert outCoercions.size() == 2;

        node.left.setCoercion(outCoercions.get(0));
        node.right.setCoercion(outCoercions.get(1));

        return binOp.retType;
    }

    @Override
    public TypeSpec visitExprCase(ExprCase node) {

        TypeSpec saveCaseSelectorType = caseSelectorType;
        List<TypeSpec> saveCaseValTypes = caseValTypes;
        caseValTypes = new ArrayList<>();
        List<TypeSpec> caseExprTypes = new ArrayList<>();

        // visit
        caseSelectorType = visit(node.selector);
        caseValTypes.add(caseSelectorType);

        TypeSpec commonType = null;
        for (CaseExpr ce : node.whenParts.nodes) {
            TypeSpec ty = visit(ce);
            commonType = getCommonType(commonType, ty);
            if (commonType == null) {
                throw new SemanticError(
                        Misc.getLineColumnOf(ce.expr.ctx), // s227
                        "expression in this case does not have a compatible type " + ty);
            }
            caseExprTypes.add(ty);
        }
        if (node.elsePart != null) {
            TypeSpec ty = visit(node.elsePart);
            commonType = getCommonType(commonType, ty);
            if (commonType == null) {
                throw new SemanticError(
                        Misc.getLineColumnOf(node.elsePart.ctx), // s228
                        "expression in the else part does not have a compatible type " + ty);
            }
            caseExprTypes.add(ty);
        }

        List<Coercion> outCoercions = new ArrayList<>();
        DeclFunc op =
                symbolStack.getOperator(outCoercions, "opIn", caseValTypes.toArray(TYPESPEC_ARR));
        if (op == null) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.ctx), // s226
                    "one of the values does not have a comparable type");
        }
        assert outCoercions.size() == caseValTypes.size();

        // set coercions of selector, case values, and case expressions
        node.selector.setCoercion(outCoercions.get(0));
        int i = 1;
        for (CaseExpr ce : node.whenParts.nodes) {
            ce.val.setCoercion(outCoercions.get(i));

            Coercion c = Coercion.getCoercion(caseExprTypes.get(i - 1), commonType);
            assert c != null
                    : ("no coercion from " + caseExprTypes.get(i - 1) + " to " + commonType);
            ce.expr.setCoercion(c);

            i++;
        }
        if (node.elsePart != null) {
            Coercion c = Coercion.getCoercion(caseExprTypes.get(i - 1), commonType);
            assert c != null
                    : ("no coercion from " + caseExprTypes.get(i - 1) + " to " + commonType);
            node.elsePart.setCoercion(c);
            i++;
        }
        assert i == caseExprTypes.size() + 1;

        node.setSelectorType(op.paramList.nodes.get(0).typeSpec);
        node.setResultType(commonType);

        caseSelectorType = saveCaseSelectorType; // restore
        caseValTypes = saveCaseValTypes; // restore

        return commonType;
    }

    @Override
    public TypeSpec visitExprCond(ExprCond node) {
        List<TypeSpec> condExprTypes = new ArrayList<>();

        TypeSpec commonType = null;
        for (CondExpr ce : node.condParts.nodes) {
            TypeSpec ty = visitCondExpr(ce);
            commonType = getCommonType(commonType, ty);
            if (commonType == null) {
                throw new SemanticError(
                        Misc.getLineColumnOf(ce.expr.ctx), // s229
                        "expression in this case does not have a compatible type " + ty);
            }
            condExprTypes.add(ty);
        }
        if (node.elsePart != null) {
            TypeSpec ty = visit(node.elsePart);
            commonType = getCommonType(commonType, ty);
            if (commonType == null) {
                throw new SemanticError(
                        Misc.getLineColumnOf(node.elsePart.ctx), // s230
                        "expression in the else part does not have a compatible type " + ty);
            }
            condExprTypes.add(ty);
        }

        int i = 0;
        for (CondExpr ce : node.condParts.nodes) {

            Coercion c = Coercion.getCoercion(condExprTypes.get(i), commonType);
            assert c != null : ("no coercion from " + condExprTypes.get(i) + " to " + commonType);
            ce.expr.setCoercion(c);

            i++;
        }
        if (node.elsePart != null) {
            Coercion c = Coercion.getCoercion(condExprTypes.get(i), commonType);
            assert c != null : ("no coercion from " + condExprTypes.get(i) + " to " + commonType);
            node.elsePart.setCoercion(c);
            i++;
        }
        assert i == condExprTypes.size();

        node.setResultType(commonType);

        return commonType;
    }

    @Override
    public TypeSpec visitExprCursorAttr(ExprCursorAttr node) {
        TypeSpec idType = visitExprId(node.id);
        assert (idType.equals(TypeSpecSimple.CURSOR)
                || idType.equals(TypeSpecSimple.SYS_REFCURSOR)); // by earlier check

        switch (node.attr) {
            case ISOPEN:
            case FOUND:
            case NOTFOUND:
                return TypeSpecSimple.BOOLEAN;
            case ROWCOUNT:
                return TypeSpecSimple.BIGINT;
            default:
                assert false : "unreachable";
                throw new RuntimeException("unreachable");
        }
    }

    @Override
    public TypeSpec visitExprDate(ExprDate node) {
        return TypeSpecSimple.DATE;
    }

    @Override
    public TypeSpec visitExprDatetime(ExprDatetime node) {
        return TypeSpecSimple.DATETIME;
    }

    @Override
    public TypeSpec visitExprFalse(ExprFalse node) {
        return TypeSpecSimple.BOOLEAN;
    }

    @Override
    public TypeSpec visitExprField(ExprField node) {
        TypeSpec ret;

        DeclId declId = node.record.decl;
        assert declId instanceof DeclForRecord;
        DeclForRecord declForRecord = (DeclForRecord) declId;
        if (declForRecord.fieldTypes != null) {
            // this record is for a static SQL

            ret = declForRecord.fieldTypes.get(node.fieldName);
            if (ret == null) {
                throw new SemanticError(
                        Misc.getLineColumnOf(node.ctx), // s401
                        String.format("no such column '%s' in the query result", node.fieldName));
            } else {

                node.setType(ret);

                int i = 1;
                for (String c : declForRecord.fieldTypes.keySet()) {
                    if (c.equals(node.fieldName)) {
                        break;
                    }
                    i++;
                }
                assert i <= declForRecord.fieldTypes.size();
                node.setColIndex(i);
            }
        } else {
            // this record is for a dynamic SQL

            ret =
                    TypeSpecSimple
                            .OBJECT; // cannot be a specific Java type: type unknown at compile time
        }

        return ret;
    }

    @Override
    public TypeSpec visitExprGlobalFuncCall(ExprGlobalFuncCall node) {
        assert node.decl != null;
        checkRoutineCall(node.decl, node.args.nodes);
        return node.decl.retType;
    }

    @Override
    public TypeSpec visitExprId(ExprId node) {
        if (node.decl instanceof DeclIdTyped) {
            return ((DeclIdTyped) node.decl).typeSpec();
        } else if (node.decl instanceof DeclCursor) {
            return TypeSpecSimple.CURSOR;
        } else if (node.decl instanceof DeclForIter) {
            return TypeSpecSimple.INT;
        } else if (node.decl instanceof DeclForRecord) {
            assert false : "unreachable";
            throw new RuntimeException("unreachable");
        } else {
            assert false : "unreachable";
            throw new RuntimeException("unreachable");
        }
    }

    @Override
    public TypeSpec visitExprIn(ExprIn node) {
        List<Expr> args = new ArrayList<>();
        List<TypeSpec> argTypes = new ArrayList<>();

        TypeSpec targetType = visit(node.target);
        args.add(node.target);
        argTypes.add(targetType);

        for (Expr e : node.inElements.nodes) {
            TypeSpec eType = visit(e);
            args.add(e);
            argTypes.add(eType);
        }
        int len = args.size();

        List<Coercion> outCoercions = new ArrayList<>();
        DeclFunc op = symbolStack.getOperator(outCoercions, "opIn", argTypes.toArray(TYPESPEC_ARR));
        if (op == null) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.ctx), // s212
                    "one of the values does not have a comparable type");
        }
        assert outCoercions.size() == len;

        for (int i = 0; i < len; i++) {
            Expr arg = args.get(i);
            Coercion c = outCoercions.get(i);
            arg.setCoercion(c);
        }

        return TypeSpecSimple.BOOLEAN;
    }

    @Override
    public TypeSpec visitExprLike(ExprLike node) {
        TypeSpec targetType = visit(node.target);
        Coercion c = Coercion.getCoercion(targetType, TypeSpecSimple.STRING);
        if (c == null) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.target.ctx), // s213
                    "tested expression cannot be coerced to STRING type");
        } else {
            node.target.setCoercion(c);
        }

        return TypeSpecSimple.BOOLEAN;
    }

    /* TODO: restore later
    @Override
    public TypeSpec visitExprList(ExprList node) {
        visitNodeList(node.elems);
        return TypeSpecSimple.LIST;
    }
     */

    @Override
    public TypeSpec visitExprLocalFuncCall(ExprLocalFuncCall node) {
        checkRoutineCall(node.decl, node.args.nodes);
        return node.decl.retType;
    }

    @Override
    public TypeSpec visitExprNull(ExprNull node) {
        return TypeSpecSimple.NULL;
    }

    @Override
    public TypeSpec visitExprUint(ExprUint node) {
        switch (node.ty) {
            case NUMERIC:
                return TypeSpecSimple.NUMERIC;
            case BIGINT:
                return TypeSpecSimple.BIGINT;
            case INT:
                return TypeSpecSimple.INT;
            default:
                assert false : "unreachable";
                throw new RuntimeException("unreachable");
        }
    }

    @Override
    public TypeSpec visitExprFloat(ExprFloat node) {
        return TypeSpecSimple.NUMERIC; // TODO: apply precision and scale
    }

    @Override
    public TypeSpec visitExprSerialVal(ExprSerialVal node) {
        assert node.verified;
        return TypeSpecSimple.NUMERIC; // TODO: apply precision and scale
    }

    @Override
    public TypeSpec visitExprSqlRowCount(ExprSqlRowCount node) {
        return TypeSpecSimple.BIGINT;
    }

    @Override
    public TypeSpec visitExprStr(ExprStr node) {
        return TypeSpecSimple.STRING;
    }

    @Override
    public TypeSpec visitExprTime(ExprTime node) {
        return TypeSpecSimple.TIME;
    }

    @Override
    public TypeSpec visitExprTrue(ExprTrue node) {
        return TypeSpecSimple.BOOLEAN;
    }

    @Override
    public TypeSpec visitExprUnaryOp(ExprUnaryOp node) {
        TypeSpec operandType = visit(node.operand);

        List<Coercion> outCoercions = new ArrayList<>();
        DeclFunc unaryOp = symbolStack.getOperator(outCoercions, "op" + node.opStr, operandType);
        if (unaryOp == null) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.ctx), // s215
                    "argument does not have a compatible type");
        }

        node.operand.setCoercion(outCoercions.get(0));

        return unaryOp.retType;
    }

    @Override
    public TypeSpec visitExprTimestamp(ExprTimestamp node) {
        return TypeSpecSimple.TIMESTAMP;
    }

    @Override
    public TypeSpec visitExprAutoParam(ExprAutoParam node) {
        return node.getTypeSpec(); // NOTE: unused yet
    }

    @Override
    public TypeSpec visitStmtAssign(StmtAssign node) {
        TypeSpec valType = visit(node.val);
        TypeSpec varType = ((DeclIdTyped) node.var.decl).typeSpec();
        Coercion c = Coercion.getCoercion(valType, varType);
        if (c == null) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.val.ctx), // s216
                    "type of the value is not compatible with the variable's type");
        } else {
            node.val.setCoercion(c);
        }

        return null;
    }

    @Override
    public TypeSpec visitStmtBasicLoop(StmtBasicLoop node) {
        visitNodeList(node.stmts);
        return null;
    }

    @Override
    public TypeSpec visitStmtBlock(StmtBlock node) {
        visitNodeList(node.decls);
        visitBody(node.body);
        return null;
    }

    @Override
    public TypeSpec visitStmtExit(StmtExit node) {
        return null; // nothing to do
    }

    @Override
    public TypeSpec visitStmtCase(StmtCase node) {

        TypeSpec saveCaseSelectorType = caseSelectorType;
        List<TypeSpec> saveCaseValTypes = caseValTypes;
        caseValTypes = new ArrayList<>();

        caseSelectorType = visit(node.selector);
        caseValTypes.add(caseSelectorType);

        visitNodeList(node.whenParts);
        if (node.elsePart != null) {
            visitNodeList(node.elsePart);
        }

        List<Coercion> outCoercions = new ArrayList<>();
        DeclFunc op =
                symbolStack.getOperator(outCoercions, "opIn", caseValTypes.toArray(TYPESPEC_ARR));
        if (op == null) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.ctx), // s201
                    "one of the values does not have a comparable type");
        }
        assert outCoercions.size() == caseValTypes.size();

        // set coercions of selector, case values, and case expressions
        node.selector.setCoercion(outCoercions.get(0));
        int i = 1;
        for (CaseStmt cs : node.whenParts.nodes) {
            cs.val.setCoercion(outCoercions.get(i));
            i++;
        }
        assert i == caseValTypes.size();

        node.setSelectorType(op.paramList.nodes.get(0).typeSpec);

        caseSelectorType = saveCaseSelectorType; // restore
        caseValTypes = saveCaseValTypes; // restore

        return null;
    }

    @Override
    public TypeSpec visitStmtCommit(StmtCommit node) {
        return null; // nothing to do
    }

    @Override
    public TypeSpec visitStmtContinue(StmtContinue node) {
        return null; // nothing to do
    }

    @Override
    public TypeSpec visitStmtCursorClose(StmtCursorClose node) {
        TypeSpec idType = visit(node.id);
        assert (idType.equals(TypeSpecSimple.CURSOR)
                || idType.equals(TypeSpecSimple.SYS_REFCURSOR)); // by earlier check
        return null;
    }

    @Override
    public TypeSpec visitStmtCursorFetch(StmtCursorFetch node) {

        TypeSpec idType = visit(node.id);
        if (idType.equals(TypeSpecSimple.CURSOR)) {
            assert node.columnTypeList != null;
        } else {
            assert idType.equals(TypeSpecSimple.SYS_REFCURSOR);
            assert node.columnTypeList == null;
        }

        List<Coercion> coercions = new ArrayList<>();

        int i = 0;
        for (ExprId intoVar : node.intoVarList) {
            TypeSpec srcTy =
                    (node.columnTypeList == null)
                            ? TypeSpecSimple.OBJECT
                            : node.columnTypeList.get(i);
            TypeSpec dstTy = ((DeclIdTyped) intoVar.decl).typeSpec();

            Coercion c = Coercion.getCoercion(srcTy, dstTy);
            if (c == null) {
                throw new SemanticError(
                        Misc.getLineColumnOf(intoVar.ctx), // s403
                        String.format(
                                "type of column %d of the cursor is not compatible with the type of variable %s",
                                i + 1, intoVar.name));
            } else {
                coercions.add(c);
            }

            i++;
        }
        node.setCoercions(coercions);

        return null;
    }

    @Override
    public TypeSpec visitStmtCursorOpen(StmtCursorOpen node) {
        TypeSpec idType = visit(node.cursor);
        if (idType.equals(TypeSpecSimple.CURSOR)) {
            DeclCursor declCursor = (DeclCursor) node.cursor.decl;
            int len = node.args.nodes.size();
            for (int i = 0; i < len; i++) {
                Expr arg = node.args.nodes.get(i);
                TypeSpec argType = visit(arg);
                TypeSpec paramType = declCursor.paramList.nodes.get(i).typeSpec();
                assert paramType != null;
                Coercion c = Coercion.getCoercion(argType, paramType);
                if (c == null) {
                    throw new SemanticError(
                            Misc.getLineColumnOf(arg.ctx), // s219
                            String.format(
                                    "argument %d to the cursor has an incompatible type", i + 1));
                } else {
                    arg.setCoercion(c);
                }
            }
        } else {
            assert false : "unreachable"; // by earlier check
            throw new RuntimeException("unreachable");
        }
        return null;
    }

    @Override
    public TypeSpec visitStmtExecImme(StmtExecImme node) {

        // type of sql must be STRING
        TypeSpec sqlType = visit(node.sql);
        if (!sqlType.equals(TypeSpecSimple.STRING)) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.sql.ctx), // s221
                    "SQL in the EXECUTE IMMEDIATE statement must be of STRING type");
        }

        // check types of expressions in USING clause
        if (node.usedExprList != null) {
            for (Expr e : node.usedExprList) {
                visit(e); // s420
            }
        }

        if (node.intoVarList != null) {

            List<Coercion> coercions = new ArrayList<>();

            // check types of into-variables
            for (ExprId intoVar : node.intoVarList) {
                TypeSpec tyIntoVar = visitExprId(intoVar);
                Coercion c = Coercion.getCoercion(TypeSpecSimple.OBJECT, tyIntoVar);
                if (c == null) {
                    throw new SemanticError( // s421
                            Misc.getLineColumnOf(intoVar.ctx),
                            "into-variable "
                                    + intoVar.name
                                    + " cannot be used there due to its incompatible type");
                } else {
                    coercions.add(c);
                }
            }
            node.setCoercions(coercions);
        }

        return null;
    }

    @Override
    public TypeSpec visitStmtStaticSql(StmtStaticSql node) {

        StaticSql staticSql = node.staticSql;

        typeCheckHostExprs(staticSql); // s404

        if (node.intoVarList != null) {

            List<Coercion> coercions = new ArrayList<>();

            // check types of into-variables
            int i = 0;
            for (String column : staticSql.selectList.keySet()) {
                TypeSpec tyColumn = staticSql.selectList.get(column);
                ExprId intoVar = node.intoVarList.get(i);
                TypeSpec tyIntoVar = visitExprId(intoVar);
                Coercion c = Coercion.getCoercion(tyColumn, tyIntoVar);
                if (c == null) {
                    throw new SemanticError( // s405
                            Misc.getLineColumnOf(staticSql.ctx),
                            "into-variable "
                                    + intoVar.name
                                    + " cannot be used there due to its incompatible type");
                } else {
                    coercions.add(c);
                }

                i++;
            }
            node.setCoercions(coercions);
        }

        return null;
    }

    @Override
    public TypeSpec visitStmtForCursorLoop(StmtForCursorLoop node) {
        visitStmtCursorOpen(node); // StmtForCursorLoop extends StmtCursorOpen
        visitNodeList(node.stmts);
        return null;
    }

    @Override
    public TypeSpec visitStmtForIterLoop(StmtForIterLoop node) {
        TypeSpec ty;

        ty = visit(node.lowerBound);
        if (!TypeSpecSimple.INT.equals(ty)) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.lowerBound.ctx), // s222
                    "lower bounds of for loops must be of INT type");
        }

        ty = visit(node.upperBound);
        if (!TypeSpecSimple.INT.equals(ty)) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.upperBound.ctx), // s223
                    "upper bounds of for loops must be of INT type");
        }

        if (node.step != null) {
            ty = visit(node.step);
            if (!TypeSpecSimple.INT.equals(ty)) {
                throw new SemanticError(
                        Misc.getLineColumnOf(node.step.ctx), // s224
                        "steps of for loops must be of INT type");
            }
        }

        visitNodeList(node.stmts);

        return null;
    }

    @Override
    public TypeSpec visitStmtForExecImmeLoop(StmtForExecImmeLoop node) {

        TypeSpec sqlType = visit(node.sql);
        if (!sqlType.equals(TypeSpecSimple.STRING)) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.sql.ctx), // s225
                    "SQL in EXECUTE IMMEDIATE statements must be of STRING type");
        }

        visitNodeList(node.stmts);

        return null;
    }

    @Override
    public TypeSpec visitStmtForStaticSqlLoop(StmtForStaticSqlLoop node) {

        typeCheckHostExprs(node.staticSql); // s406
        visitNodeList(node.stmts);
        return null;
    }

    @Override
    public TypeSpec visitStmtGlobalProcCall(StmtGlobalProcCall node) {
        assert node.decl != null;
        checkRoutineCall(node.decl, node.args.nodes);
        return null;
    }

    @Override
    public TypeSpec visitStmtIf(StmtIf node) {
        visitNodeList(node.condStmtParts);
        if (node.elsePart != null) {
            visitNodeList(node.elsePart);
        }
        return null;
    }

    @Override
    public TypeSpec visitStmtLocalProcCall(StmtLocalProcCall node) {
        checkRoutineCall(node.decl, node.args.nodes);
        return null;
    }

    @Override
    public TypeSpec visitStmtNull(StmtNull node) {
        return null; // nothing to do
    }

    @Override
    public TypeSpec visitStmtOpenFor(StmtOpenFor node) {
        TypeSpec ty = visitExprId(node.id);
        assert ty.equals(TypeSpecSimple.SYS_REFCURSOR); // by earlier check

        assert node.staticSql != null;
        assert node.staticSql.intoVars == null; // by earlier check

        typeCheckHostExprs(node.staticSql); // s407
        return null;
    }

    @Override
    public TypeSpec visitStmtRaise(StmtRaise node) {
        return null; // nothing to do
    }

    @Override
    public TypeSpec visitStmtRaiseAppErr(StmtRaiseAppErr node) {
        TypeSpec ty;

        ty = visit(node.errCode);
        if (!ty.equals(TypeSpecSimple.INT)) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.errCode.ctx), // s220
                    "error codes must be an INT");
        }

        ty = visit(node.errMsg);
        if (!ty.equals(TypeSpecSimple.STRING)) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.errMsg.ctx), // s218
                    "error messages must be a string");
        }

        return null;
    }

    @Override
    public TypeSpec visitStmtReturn(StmtReturn node) {
        if (node.retVal != null) {
            TypeSpec valType = visit(node.retVal);
            Coercion c = Coercion.getCoercion(valType, node.retType);
            if (c == null) {
                throw new SemanticError(
                        Misc.getLineColumnOf(node.retVal.ctx), // s217
                        "type of the return value is not compatible with the return type");
            } else {
                node.retVal.setCoercion(c);
            }
        }
        return null;
    }

    @Override
    public TypeSpec visitStmtRollback(StmtRollback node) {
        return null; // nothing to do
    }

    @Override
    public TypeSpec visitStmtWhileLoop(StmtWhileLoop node) {
        TypeSpec condType = visit(node.cond);
        if (!condType.equals(TypeSpecSimple.BOOLEAN)) {
            throw new SemanticError(
                    Misc.getLineColumnOf(node.cond.ctx), // s211
                    "while loops' condition must be of BOOLEAN type");
        }
        visitNodeList(node.stmts);
        return null;
    }

    @Override
    public TypeSpec visitExName(ExName node) {
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ------------------------------------------------------------------
    // Private
    // ------------------------------------------------------------------

    private static final TypeSpec[] TYPESPEC_ARR = new TypeSpec[0];

    private SymbolStack symbolStack;

    private TypeSpec caseSelectorType;
    private List<TypeSpec> caseValTypes;

    private TypeSpec getCommonType(TypeSpec former, TypeSpec delta) {
        if (former == null) {
            return delta;
        } else {
            return CoercionScheme.getCommonType(former, delta);
        }
    }

    private void visitDeclParam(DeclParam node) {
        visit(node.typeSpec);
    }

    private TypeSpec visitDeclRoutine(DeclRoutine node) {
        visitNodeList(node.paramList);
        if (node.retType != null) {
            visit(node.retType);
        }
        if (node.decls != null) {
            visitNodeList(node.decls);
        }
        assert node.body != null; // syntactically guaranteed
        visitBody(node.body);
        return null;
    }

    private void checkRoutineCall(DeclRoutine decl, List<Expr> args) {
        int len = args.size();
        for (int i = 0; i < len; i++) {
            Expr arg = args.get(i);
            TypeSpec argType = visit(arg);
            TypeSpec paramType = decl.paramList.nodes.get(i).typeSpec();
            assert paramType
                    != null; // TODO: paramType can be null if variadic parameters are introduced
            Coercion c = Coercion.getCoercion(argType, paramType);
            if (c == null) {
                throw new SemanticError(
                        Misc.getLineColumnOf(arg.ctx), // s214
                        String.format(
                                "argument %d to the call of %s has an incompatible type",
                                i + 1, decl.name));
            } else {
                arg.setCoercion(c);
            }
        }
    }

    private void typeCheckHostExprs(StaticSql staticSql) {

        assert staticSql.ctx != null;

        LinkedHashMap<Expr, TypeSpec> hostExprs = staticSql.hostExprs;
        for (Expr e : hostExprs.keySet()) {
            TypeSpec ty = visit(e);

            TypeSpec tyRequired = hostExprs.get(e);
            if (tyRequired != null) {
                // NOTE: unreachable for now. but remains for future extension
                assert e instanceof ExprId;

                ExprId id = (ExprId) e;
                Coercion c = Coercion.getCoercion(ty, tyRequired);
                if (c == null) {
                    throw new SemanticError(
                            Misc.getLineColumnOf(staticSql.ctx),
                            "host variable "
                                    + id.name
                                    + " does not have a compatible type in the SQL statement");
                } else {
                    // no more use of the coercion: coercion (if not an identity) will be done in
                    // the
                    // server
                }
            }
        }
    }
}
