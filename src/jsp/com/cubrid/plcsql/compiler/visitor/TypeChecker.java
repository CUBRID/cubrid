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
import com.cubrid.plcsql.compiler.GlobalTypeInfo;
import com.cubrid.plcsql.compiler.SemanticError;
import com.cubrid.plcsql.compiler.SymbolStack;
import com.cubrid.plcsql.compiler.ast.*;
import java.util.List;
import java.util.ArrayList;
import java.util.LinkedHashMap;

public class TypeChecker extends AstVisitor<TypeSpec> {

    public TypeChecker(SymbolStack symbolStack, GlobalTypeInfo gti) {
        this.symbolStack = symbolStack;
        this.gti = gti;
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

    @Override
    public TypeSpec visitTypeSpecNumeric(TypeSpecNumeric node) {
        return null; // nothing to do
    }

    @Override
    public TypeSpec visitTypeSpecPercent(TypeSpecPercent node) {
        TypeSpec ty = gti.getTableColumnType(node.table, node.column);
        assert ty != null;
        node.setResolvedType(ty);
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
                    "type of the condition must be boolean");
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
                        "not null variables may not have null as its initial value");
            }

            Coerce c = Coerce.getCoerce(valType, node.typeSpec);
            if (c == null) {
                throw new SemanticError(
                        node.val.lineNo(), // s205
                        "the initial value's type is not compatible with the variable's declared type");
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
                    "not null constants may not have null as its initial value");
        }

        Coerce c = Coerce.getCoerce(valType, node.typeSpec);
        if (c == null) {
            throw new SemanticError(
                    node.val.lineNo(), // s207
                    "the initial value's type is not compatible with the constant's declared type");
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
        visitNodeList(node.paramList);

        // TODO: requires server API
        assert false : "server semantic API required: not implemented yet";
        throw new RuntimeException("server semantic API required: not implemented yet");
        //return null;
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

        if (!areComparableTypes(targetType, lowerType)) {
            throw new SemanticError(
                    node.lowerBound.lineNo(), // s208
                    "lower bound has an incomparable type in the 'between' expression");
        }

        TypeSpec upperType = visit(node.upperBound);

        if (!areComparableTypes(targetType, upperType)) {
            throw new SemanticError(
                    node.upperBound.lineNo(), // s209
                    "upper bound has an incomparable type in the 'between' expression");
        }

        return TypeSpecSimple.BOOLEAN;
    }

    @Override
    public TypeSpec visitExprBinaryOp(ExprBinaryOp node) {
        TypeSpec leftType = visit(node.left);
        TypeSpec rightType = visit(node.right);
        DeclFunc binOp = symbolStack.getOperator("op" + node.opStr, leftType, rightType);
        if (binOp == null) {
            throw new SemanticError(
                    node.lineNo(), // s210
                    "binary operator '"
                            + node.opStr.toLowerCase()
                            + "' cannot be applied to the argument types");
        }

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
        if (node.elsePart != null) {
            TypeSpec ty = visit(node.elsePart);
            commonType = getCommonType(commonType, ty);
        }

        caseSelectorType = saveCaseSelectorType; // restore

        node.setResultType(commonType);
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
        if (node.elsePart != null) {
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
                || idType.equals(TypeSpecSimple.REFCURSOR)); // by earlier check

        switch (node.attr) {
            case ISOPEN:
            case FOUND:
            case NOTFOUND:
                return TypeSpecSimple.BOOLEAN;
            case ROWCOUNT:
                return TypeSpecSimple.LONG;
            default:
                assert false : "unreachable";
                throw new RuntimeException("unreachable");
        }
    }

    @Override
    public TypeSpec visitExprDate(ExprDate node) {
        return TypeSpecSimple.LOCALDATE;
    }

    @Override
    public TypeSpec visitExprDatetime(ExprDatetime node) {
        return TypeSpecSimple.LOCALDATETIME;
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
        if (declForRecord.forDynamicSql) {
            ret = TypeSpecSimple.UNKNOWN;
        } else if (declForRecord.columns != null) {

            // this record is for a static SQL

            ret = declForRecord.columns.get(node.fieldName);
            if (ret == null) {
                throw new SemanticError(
                        node.lineNo(), // s400
                        String.format(
                                "no such column '%s' in the query result",
                                node.fieldName));
            }
        } else {
            assert false: "unreachable";
            throw new RuntimeException("unreachable");
        }

        return ret;
    }

    @Override
    public TypeSpec visitExprGlobalFuncCall(ExprGlobalFuncCall node) {
        DeclFunc declFunc = gti.getDeclFunc(node.name);
        assert declFunc != null;

        checkRoutineCall(declFunc, node.args.nodes);
        return declFunc.retType;
    }

    @Override
    public TypeSpec visitExprId(ExprId node) {
        if (node.decl instanceof DeclVarLike) {
            return ((DeclVarLike) node.decl).typeSpec();
        } else if (node.decl instanceof DeclCursor) {
            return TypeSpecSimple.CURSOR;
        } else if (node.decl instanceof DeclForIter) {
            return TypeSpecSimple.INTEGER;
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
        TypeSpec targetType = visit(node.target);
        int i = 1;
        for (Expr e : node.inElements.nodes) {
            TypeSpec eType = visit(e);
            if (!areComparableTypes(targetType, eType)) {
                throw new SemanticError(
                        e.lineNo(), // s212
                        "element " + i + " has an incomparable type");
            }
            i++;
        }
        return TypeSpecSimple.BOOLEAN;
    }

    @Override
    public TypeSpec visitExprLike(ExprLike node) {
        TypeSpec targetType = visit(node.target);
        if (!targetType.equals(TypeSpecSimple.STRING)) {
            throw new SemanticError(
                    node.target.lineNo(), // s213
                    "tested expression is not of the String type");
        }
        return TypeSpecSimple.BOOLEAN;
    }

    @Override
    public TypeSpec visitExprList(ExprList node) {
        visitNodeList(node.elems);
        return TypeSpecSimple.LIST;
    }

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
            case BIGDECIMAL:
                return TypeSpecSimple.BIGDECIMAL;
            case LONG:
                return TypeSpecSimple.LONG;
            case INTEGER:
                return TypeSpecSimple.INTEGER;
            default:
                assert false : "unreachable";
                throw new RuntimeException("unreachable");
        }
    }

    @Override
    public TypeSpec visitExprFloat(ExprFloat node) {
        return TypeSpecSimple.BIGDECIMAL; // TODO: apply precision and scale
    }

    @Override
    public TypeSpec visitExprSerialVal(ExprSerialVal node) {
        assert gti.isSerial(node.name);
        return TypeSpecSimple.BIGDECIMAL; // TODO: apply precision and scale
    }

    @Override
    public TypeSpec visitExprSqlRowCount(ExprSqlRowCount node) {
        return TypeSpecSimple.LONG;
    }

    @Override
    public TypeSpec visitExprStr(ExprStr node) {
        return TypeSpecSimple.STRING;
    }

    @Override
    public TypeSpec visitExprTime(ExprTime node) {
        return TypeSpecSimple.LOCALTIME;
    }

    @Override
    public TypeSpec visitExprTrue(ExprTrue node) {
        return TypeSpecSimple.BOOLEAN;
    }

    @Override
    public TypeSpec visitExprUnaryOp(ExprUnaryOp node) {
        TypeSpec operandType = visit(node.operand);
        DeclFunc unaryOp = symbolStack.getOperator("op" + node.opStr, operandType);
        if (unaryOp == null) {
            throw new SemanticError(
                    node.lineNo(), // s215
                    "the unary operator '"
                            + node.opStr.toLowerCase()
                            + "' cannot be applied to the argument type");
        }
        return unaryOp.retType;
    }

    @Override
    public TypeSpec visitExprZonedDateTime(ExprZonedDateTime node) {
        return TypeSpecSimple.ZONEDDATETIME;
    }

    @Override
    public TypeSpec visitStmtAssign(StmtAssign node) {
        TypeSpec valType = visit(node.val);
        TypeSpec varType = ((DeclVarLike) node.var.decl).typeSpec();
        Coerce c = Coerce.getCoerce(valType, varType);
        if (c == null) {
            throw new SemanticError(
                    node.val.lineNo(), // s216
                    "assigned value has incompatible type with the variable's type");
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
                || idType.equals(TypeSpecSimple.REFCURSOR)); // by earlier check
        return null;
    }

    @Override
    public TypeSpec visitStmtCursorFetch(StmtCursorFetch node) {

        TypeSpec idType = visit(node.id);
        if (idType.equals(TypeSpecSimple.CURSOR)) {

            List<Coerce> coerces = new ArrayList<>();

            DeclCursor declCursor = (DeclCursor) node.id.decl;
            LinkedHashMap<String, TypeSpec> columns = declCursor.columns;
            assert columns != null;

            int len = node.intoVars.nodes.size();
            if (columns.size() < len) {
                throw new SemanticError(    // TODO: verify what happens in Oracle
                        node.lineNo(), // s401
                        "the number of columns of the cursor is less than the number of into-variables");
            }
            int i = 0;
            for (String column: columns.keySet()) {
                TypeSpec columnType = columns.get(column);
                ExprId intoVar = node.intoVars.nodes.get(i);
                Coerce c = Coerce.getCoerce(columnType, ((DeclVarLike) intoVar.decl).typeSpec());
                if (c == null) {
                    throw new SemanticError(
                            intoVar.lineNo(), // s402
                            String.format("the type of column %d of the cursor is not compatible with the variable %s",
                                i + 1, intoVar.name));
                }
                coerces.add(c);

                i++;
                if (i == len) {
                    break;
                }
            }
            node.setCoerces(coerces);

        } else if (idType.equals(TypeSpecSimple.REFCURSOR)) {
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
        DeclCursor declCursor = (DeclCursor) node.cursor.decl;
        if (idType.equals(TypeSpecSimple.CURSOR)) {
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
                            String.format("argument %d to cursor has an incompatible type", i + 1));
                }
                arg.setCoerce(c);
            }
        } else {
            assert false : "unreachable"; // by earlier check
            throw new RuntimeException("unreachable");
        }
        return null;
    }

    @Override
    public TypeSpec visitStmtExecImme(StmtExecImme node) {

        if (node.isDynamic) {
            TypeSpec sqlType = visit(node.sql);
            if (!sqlType.equals(TypeSpecSimple.STRING)) {
                throw new SemanticError(
                        node.sql.lineNo(), // s221
                        "SQL in the EXECUTE IMMEDIATE statement must be of the STRING type");
            }
        } else {
            // host variables must have types that are compatible with the required types

            // In case of a select statement,
            // select list must have a larger number than the into variables
            // and types compatible with the declared types of into variables
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
        if (!TypeSpecSimple.INTEGER.equals(ty)) {
            throw new SemanticError(
                    node.lowerBound.lineNo(), // s222
                    "lower bound of for loop must be of the INTEGER type");
        }

        ty = visit(node.upperBound);
        if (!TypeSpecSimple.INTEGER.equals(ty)) {
            throw new SemanticError(
                    node.upperBound.lineNo(), // s223
                    "upper bound of for loop must be of the INTEGER type");
        }

        if (node.step != null) {
            ty = visit(node.step);
            if (!TypeSpecSimple.INTEGER.equals(ty)) {
                throw new SemanticError(
                        node.step.lineNo(), // s224
                        "step of for loop must be of the INTEGER type");
            }
        }

        visitNodeList(node.stmts);

        return null;
    }

    @Override
    public TypeSpec visitStmtForSqlLoop(StmtForSqlLoop node) {

        if (node.isDynamic) {
            TypeSpec sqlType = visit(node.sql);
            if (!sqlType.equals(TypeSpecSimple.STRING)) {
                throw new SemanticError(
                        node.sql.lineNo(), // s225
                        "SQL in the EXECUTE IMMEDIATE statement must be of the STRING type");
            }
        } else {
            // TODO: check if it is a SELECT statement

            // TODO: requires server API
            assert false : "server semantic API required: not implemented yet";
            throw new RuntimeException("server semantic API required: not implemented yet");
        }

        return null;
    }

    @Override
    public TypeSpec visitStmtGlobalProcCall(StmtGlobalProcCall node) {
        DeclProc declProc = gti.getDeclProc(node.name);
        assert declProc != null;

        checkRoutineCall(declProc, node.args.nodes);
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
        assert ty.equals(TypeSpecSimple.REFCURSOR); // by earlier check

        // TODO: requires server API
        assert false : "server semantic API required: not implemented yet";
        throw new RuntimeException("server semantic API required: not implemented yet");
        // return null;
    }

    @Override
    public TypeSpec visitStmtRaise(StmtRaise node) {
        return null; // nothing to do
    }

    @Override
    public TypeSpec visitStmtRaiseAppErr(StmtRaiseAppErr node) {
        TypeSpec ty;

        ty = visit(node.errCode);
        if (!ty.equals(TypeSpecSimple.INTEGER)) {
            throw new SemanticError(
                    node.errCode.lineNo(), // s220
                    "error code must be an integer");
        }

        ty = visit(node.errMsg);
        if (!ty.equals(TypeSpecSimple.STRING)) {
            throw new SemanticError(
                    node.errMsg.lineNo(), // s218
                    "error message must be a string");
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
                        "return value has a type incompatible with the return type");
            }
            node.retVal.setCoerce(c);
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
                    "condition expressions of while loops must be of the BOOLEAN type");
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

    private SymbolStack symbolStack;
    private GlobalTypeInfo gti;

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
                                "argument %d to %s call has an incompatible type",
                                i + 1, decl.name));
            }
            arg.setCoerce(c);
        }
    }
}
