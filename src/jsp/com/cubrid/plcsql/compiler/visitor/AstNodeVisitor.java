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

import com.cubrid.plcsql.compiler.ast.*;
import com.cubrid.plcsql.compiler.SemanticError;

public class AstNodeVisitor<R> {
    public R visit(AstNode node) {
        return node.accept(this);
    }
    public <E extends AstNode> TypeSpec visitNodeList(NodeList<E> nodeList) {
        for (E e: nodeList.nodes) {
            visit(e);
        }
        return null;
    }

    public R visitUnit(Unit node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitBody(Body node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }

    public R visitExHandler(ExHandler node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExName(ExName node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }

    public R visitTypeSpecNumeric(TypeSpecNumeric node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitTypeSpecPercent(TypeSpecPercent node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitTypeSpecSimple(TypeSpecSimple node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }

    public R visitCaseExpr(CaseExpr node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitCaseStmt(CaseStmt node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitCondExpr(CondExpr node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitCondStmt(CondStmt node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }

    public R visitDeclParamIn(DeclParamIn node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitDeclParamOut(DeclParamOut node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }

    public R visitDeclVar(DeclVar node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitDeclConst(DeclConst node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }

    public R visitDeclFunc(DeclFunc node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitDeclProc(DeclProc node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }

    public R visitDeclCursor(DeclCursor node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitDeclLabel(DeclLabel node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitDeclException(DeclException node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }

    public R visitExprBetween(ExprBetween node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprBinaryOp(ExprBinaryOp node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprCase(ExprCase node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprCond(ExprCond node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprCursorAttr(ExprCursorAttr node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprDate(ExprDate node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprDatetime(ExprDatetime node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprFalse(ExprFalse node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprField(ExprField node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprGlobalFuncCall(ExprGlobalFuncCall node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprId(ExprId node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprIn(ExprIn node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprLike(ExprLike node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprList(ExprList node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprLocalFuncCall(ExprLocalFuncCall node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprNull(ExprNull node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprUint(ExprUint node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprFloat(ExprFloat node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprSerialVal(ExprSerialVal node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprSqlRowCount(ExprSqlRowCount node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprStr(ExprStr node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprTime(ExprTime node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprTrue(ExprTrue node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprUnaryOp(ExprUnaryOp node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitExprZonedDateTime(ExprZonedDateTime node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }

    public R visitStmtAssign(StmtAssign node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtBasicLoop(StmtBasicLoop node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtBlock(StmtBlock node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtBreak(StmtBreak node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtCase(StmtCase node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtCommit(StmtCommit node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtContinue(StmtContinue node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtCursorClose(StmtCursorClose node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtCursorFetch(StmtCursorFetch node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtCursorOpen(StmtCursorOpen node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtExecImme(StmtExecImme node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtForCursorLoop(StmtForCursorLoop node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtForIterLoop(StmtForIterLoop node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtForSqlLoop(StmtForSqlLoop node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtGlobalProcCall(StmtGlobalProcCall node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtIf(StmtIf node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtLocalProcCall(StmtLocalProcCall node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtNull(StmtNull node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtOpenFor(StmtOpenFor node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtRaise(StmtRaise node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtRaiseAppErr(StmtRaiseAppErr node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtReturn(StmtReturn node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtRollback(StmtRollback node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }
    public R visitStmtWhileLoop(StmtWhileLoop node) {
        assert false: "not overriden";
        throw new RuntimeException("not overriden");
    }

    /* no need to visit the following classes
    # super classes/inteerfaces
    AstNode
    NodeList
    Decl
    DeclBase
    DeclId
    DeclParam
    DeclRoutine
    DeclVarLike
    Expr
    Stmt
    TypeSpec

    # actually no ast nodes
    DeclForIter
    DeclForRecord

    # dummies
    DummyExpr
    DummyStmt
    ExprCast
     */
}

