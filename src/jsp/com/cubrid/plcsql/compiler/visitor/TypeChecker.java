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

import com.cubrid.plcsql.compiler.Coerce;
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
        if (!areComparableTypes(caseSelectorType, caseValType)) {
            throw new SemanticError(
                    node.val.lineNo(), // s200
                    "case value is not comparable with the case selector: incomparable types");
        }
        return visit(node.expr);
    }

    @Override
    public TypeSpec visitCaseStmt(CaseStmt node) {
        TypeSpec caseValType = visit(node.val);
        if (!areComparableTypes(caseSelectorType, caseValType)) {
            throw new SemanticError(
                    node.val.lineNo(), // s201
                    "case value is not comparable with the case selector: incomparable types");
        }
        visitNodeList(node.stmts);
        return null;
    }

    @Override
    public TypeSpec visitCondExpr(CondExpr node) {
        TypeSpec caseCondType = visit(node.cond);
        if (!caseCondType.equals(TypeSpecSimple.BOOLEAN)) {
            throw new SemanticError(
                    node.cond.lineNo(), // s202
                    "the condition must be boolean");
        }
        return visit(node.expr);
    }

    @Override
    public TypeSpec visitCondStmt(CondStmt node) {
        TypeSpec caseCondType = visit(node.cond);
        if (!caseCondType.equals(TypeSpecSimple.BOOLEAN)) {
            throw new SemanticError(
                    node.cond.lineNo(), // s203
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
                        node.val.lineNo(), // s204
                        "NOT NULL variables may not have null as their initial value");
            }

            Coerce c = Coerce.getCoerce(valType, node.typeSpec);
            if (c == null) {
                throw new SemanticError(
                        node.val.lineNo(), // s205
                        "type of the initial value is not compatible with the variable's declared type");
            } else {
                node.val.setCoerce(c);
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
                    node.val.lineNo(), // s206
                    "NOT NULL constants may not have null as their initial value");
        }

        Coerce c = Coerce.getCoerce(valType, node.typeSpec);
        if (c == null) {
            throw new SemanticError(
                    node.val.lineNo(), // s207
                    "type of the initial value is not compatible with the constant's declared type");
        } else {
            node.val.setCoerce(c);
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

        List<Coerce> outCoercions = new ArrayList<>();
        DeclFunc op = symbolStack.getOperator(outCoercions, "opBetween",    // s208, s209
            node.lineNo(), targetType, lowerType, upperType);
        assert op != null;
        assert outCoercions.size() == 3;

        node.target.setCoerce(outCoercions.get(0));
        node.lowerBound.setCoerce(outCoercions.get(1));
        node.upperBound.setCoerce(outCoercions.get(2));

        return TypeSpecSimple.BOOLEAN;
    }

    @Override
    public TypeSpec visitExprBinaryOp(ExprBinaryOp node) {
        TypeSpec leftType = visit(node.left);
        TypeSpec rightType = visit(node.right);

        // in the following line, s210 (no match), s226 (ambiguous matches)
        List<Coerce> outCoercions = new ArrayList<>();
        DeclFunc binOp = symbolStack.getOperator(outCoercions, "op" + node.opStr, node.lineNo(), leftType, rightType);
        assert binOp != null;

        node.left.setCoerce(outCoercions.get(0));
        node.right.setCoerce(outCoercions.get(1));

        return binOp.retType;
    }

    @Override
    public TypeSpec visitExprCase(ExprCase node) {

        // TODO: check the cases when selector or case value is NULL at
        // compile time and at runtime in CUBRID and Oracle

        TypeSpec saveCaseSelectorType = caseSelectorType;
        caseSelectorType = visit(node.selector);
        node.setSelectorType(caseSelectorType);

        TypeSpec commonType = null;
        for (CaseExpr ce : node.whenParts.nodes) {
            TypeSpec ty = visit(ce);
            if (commonType == null) {
                commonType = ty;
            } else {
                commonType = getCommonType(commonType, ty);
            }
        }
        if (node.elsePart == null) {
            if (commonType.equals(TypeSpecSimple.NULL)) {
                commonType =
                        TypeSpecSimple
                                .OBJECT; // cannot be a specific Java type: there is no Null type in
                // Java
            }
        } else {
            TypeSpec ty = visit(node.elsePart);
            commonType = getCommonType(commonType, ty);
        }

        node.setResultType(commonType);

        caseSelectorType = saveCaseSelectorType; // restore
        return commonType;
    }

    @Override
    public TypeSpec visitExprCond(ExprCond node) {
        TypeSpec commonType = null;
        for (CondExpr ce : node.condParts.nodes) {
            TypeSpec ty = visitCondExpr(ce);
            if (commonType == null) {
                commonType = ty;
            } else {
                commonType = getCommonType(commonType, ty);
            }
        }
        if (node.elsePart == null) {
            if (commonType.equals(TypeSpecSimple.NULL)) {
                commonType =
                        TypeSpecSimple
                                .OBJECT; // cannot be a specific Java type: there is no Null type in
                // Java
            }
        } else {
            TypeSpec ty = visit(node.elsePart);
            commonType = getCommonType(commonType, ty);
        }

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
                        node.lineNo(), // s401
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

        List<Coerce> outCoercions = new ArrayList<>();
        DeclFunc op = symbolStack.getOperator(outCoercions, "opIn", node.lineNo(), argTypes.toArray(tsArr)); // s212
        assert op != null;
        assert outCoercions.size() == len;

        for (int i = 0; i < len; i++) {
            Expr arg = args.get(i);
            Coerce c = outCoercions.get(i);
            arg.setCoerce(c);
        }

        return TypeSpecSimple.BOOLEAN;
    }

    @Override
    public TypeSpec visitExprLike(ExprLike node) {
        TypeSpec targetType = visit(node.target);
        Coerce c = Coerce.getCoerce(targetType, TypeSpecSimple.STRING);
        if (c == null) {
            throw new SemanticError(
                    node.target.lineNo(), // s213
                    "tested expression cannot be coerced to STRING type");
        } else {
            node.target.setCoerce(c);
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

        // in the following line, s215 (no match), s227 (ambiguous matches)
        List<Coerce> outCoercions = new ArrayList<>();
        DeclFunc unaryOp = symbolStack.getOperator(outCoercions, "op" + node.opStr, node.lineNo(), operandType);
        assert unaryOp != null;

        node.operand.setCoerce(outCoercions.get(0));

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
        Coerce c = Coerce.getCoerce(valType, varType);
        if (c == null) {
            throw new SemanticError(
                    node.val.lineNo(), // s216
                    "type of the value is not compatible with the variable's type");
        } else {
            node.val.setCoerce(c);
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
    public TypeSpec visitStmtBreak(StmtBreak node) {
        return null; // nothing to do
    }

    @Override
    public TypeSpec visitStmtCase(StmtCase node) {
        TypeSpec saveCaseSelectorType = caseSelectorType;
        caseSelectorType = visit(node.selector);
        node.setSelectorType(caseSelectorType);

        visitNodeList(node.whenParts);
        if (node.elsePart != null) {
            visitNodeList(node.elsePart);
        }

        caseSelectorType = saveCaseSelectorType; // restore
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

            List<Coerce> coerces = new ArrayList<>();

            DeclCursor declCursor = (DeclCursor) node.id.decl;
            assert declCursor.staticSql != null;
            LinkedHashMap<String, TypeSpec> selectList = declCursor.staticSql.selectList;

            int i = 0;
            for (String column : selectList.keySet()) {
                TypeSpec columnType = selectList.get(column);
                ExprId intoVar = node.intoVars.nodes.get(i);
                Coerce c = Coerce.getCoerce(columnType, ((DeclIdTyped) intoVar.decl).typeSpec());
                if (c == null) {
                    throw new SemanticError(
                            intoVar.lineNo(), // s403
                            String.format(
                                    "type of column %d of the cursor is not compatible with the type of variable %s",
                                    i + 1, intoVar.name));
                } else {
                    coerces.add(c);
                }

                i++;
            }
            node.setCoerces(coerces);

        } else if (idType.equals(TypeSpecSimple.SYS_REFCURSOR)) {
            // nothing to do more,
        } else {
            assert false : "unreachable"; // by earlier check
            throw new RuntimeException("unreachable");
        }

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
                Coerce c = Coerce.getCoerce(argType, paramType);
                if (c == null) {
                    throw new SemanticError(
                            arg.lineNo(), // s219
                            String.format(
                                    "argument %d to the cursor has an incompatible type", i + 1));
                } else {
                    arg.setCoerce(c);
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

        TypeSpec sqlType = visit(node.sql);
        if (!sqlType.equals(TypeSpecSimple.STRING)) {
            throw new SemanticError(
                    node.sql.lineNo(), // s221
                    "SQL in the EXECUTE IMMEDIATE statement must be of STRING type");
        }

        return null;
    }

    @Override
    public TypeSpec visitStmtStaticSql(StmtStaticSql node) {

        StaticSql staticSql = node.staticSql;

        typeCheckHostExprs(staticSql); // s404
        if (staticSql.intoVars != null) {

            List<Coerce> coerces = new ArrayList<>();

            // check types of into-variables
            int i = 0;
            for (String column : staticSql.selectList.keySet()) {
                TypeSpec tyColumn = staticSql.selectList.get(column);
                ExprId intoVar = staticSql.intoVars.get(i);
                TypeSpec tyIntoVar = visitExprId(intoVar);
                Coerce c = Coerce.getCoerce(tyColumn, tyIntoVar);
                if (c == null) {
                    throw new SemanticError( // s405
                            Misc.getLineOf(staticSql.ctx),
                            "into-variable "
                                    + intoVar.name
                                    + " cannot be used there due to its incompatible type");
                } else {
                    coerces.add(c);
                }

                i++;
            }
            node.setCoerces(coerces);
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
                    node.lowerBound.lineNo(), // s222
                    "lower bounds of for loops must be of INT type");
        }

        ty = visit(node.upperBound);
        if (!TypeSpecSimple.INT.equals(ty)) {
            throw new SemanticError(
                    node.upperBound.lineNo(), // s223
                    "upper bounds of for loops must be of INT type");
        }

        if (node.step != null) {
            ty = visit(node.step);
            if (!TypeSpecSimple.INT.equals(ty)) {
                throw new SemanticError(
                        node.step.lineNo(), // s224
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
                    node.sql.lineNo(), // s225
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
                    node.errCode.lineNo(), // s220
                    "error codes must be an INT");
        }

        ty = visit(node.errMsg);
        if (!ty.equals(TypeSpecSimple.STRING)) {
            throw new SemanticError(
                    node.errMsg.lineNo(), // s218
                    "error messages must be a string");
        }

        return null;
    }

    @Override
    public TypeSpec visitStmtReturn(StmtReturn node) {
        if (node.retVal != null) {
            TypeSpec valType = visit(node.retVal);
            Coerce c = Coerce.getCoerce(valType, node.retType);
            if (c == null) {
                throw new SemanticError(
                        node.retVal.lineNo(), // s217
                        "type of the return value is not compatible with the return type");
            } else {
                node.retVal.setCoerce(c);
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
                    node.cond.lineNo(), // s211
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

    private static final TypeSpec[] tsArr = new TypeSpec[0];

    private SymbolStack symbolStack;

    private TypeSpec caseSelectorType;

    private boolean areComparableTypes(TypeSpec l, TypeSpec r) {
        return l.equals(r); // TODO: consider implicit type conversion
    }

    private TypeSpec getCommonType(TypeSpec l, TypeSpec r) {
        return l.equals(r) ? l : null; // TODO: complete
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
            Coerce c = Coerce.getCoerce(argType, paramType);
            if (c == null) {
                throw new SemanticError(
                        arg.lineNo(), // s214
                        String.format(
                                "argument %d to the call of %s has an incompatible type",
                                i + 1, decl.name));
            } else {
                arg.setCoerce(c);
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
                // NOTE: Unreachable for now but for future extension
                assert e instanceof ExprId;

                ExprId id = (ExprId) e;
                Coerce c = Coerce.getCoerce(ty, tyRequired);
                if (c == null) {
                    throw new SemanticError(
                            Misc.getLineOf(staticSql.ctx),
                            "host variable "
                                    + id.name
                                    + " does not have a compatible type in the SQL statement");
                } else {
                    // no more use of the coerce: coercion (if not an identity) will be done in the
                    // server
                }
            }
        }
    }
}
