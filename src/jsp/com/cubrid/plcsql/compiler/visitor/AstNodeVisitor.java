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

public class AstNodeVisitor<R> {
    public R visit(AstNode node) { return node.accept(this); }

    public R visitUnit(Unit node) { return null; }
    public R visitBody(Body node) { return null; }

    public R visitExHandler(ExHandler node) { return null; }
    public R visitExName(ExName node) { return null; }

    public R visitTypeSpecNumeric(TypeSpecNumeric node) { return null; }
    public R visitTypeSpecPercent(TypeSpecPercent node) { return null; }
    public R visitTypeSpecSimple(TypeSpecSimple node) { return null; }

    public R visitCaseExpr(CaseExpr node) { return null; }
    public R visitCaseStmt(CaseStmt node) { return null; }
    public R visitCondExpr(CondExpr node) { return null; }
    public R visitCondStmt(CondStmt node) { return null; }

    public R visitDeclParamIn(DeclParamIn node) { return null; }
    public R visitDeclParamOut(DeclParamOut node) { return null; }

    public R visitDeclVar(DeclVar node) { return null; }
    public R visitDeclConst(DeclConst node) { return null; }

    public R visitDeclFunc(DeclFunc node) { return null; }
    public R visitDeclProc(DeclProc node) { return null; }

    public R visitDeclCursor(DeclCursor node) { return null; }
    public R visitDeclForIter(DeclForIter node) { return null; }
    public R visitDeclForRecord(DeclForRecord node) { return null; }

    public R visitDeclLabel(DeclLabel node) { return null; }
    public R visitDeclException(DeclException node) { return null; }

    public R visitExprBetween(ExprBetween node) { return null; }
    public R visitExprBinaryOp(ExprBinaryOp node) { return null; }
    public R visitExprCase(ExprCase node) { return null; }
    public R visitExprCast(ExprCast node) { return null; }
    public R visitExprCond(ExprCond node) { return null; }
    public R visitExprCursorAttr(ExprCursorAttr node) { return null; }
    public R visitExprDate(ExprDate node) { return null; }
    public R visitExprDatetime(ExprDatetime node) { return null; }
    public R visitExprFalse(ExprFalse node) { return null; }
    public R visitExprField(ExprField node) { return null; }
    public R visitExprGlobalFuncCall(ExprGlobalFuncCall node) { return null; }
    public R visitExprId(ExprId node) { return null; }
    public R visitExprIn(ExprIn node) { return null; }
    public R visitExprLike(ExprLike node) { return null; }
    public R visitExprList(ExprList node) { return null; }
    public R visitExprLocalFuncCall(ExprLocalFuncCall node) { return null; }
    public R visitExprNull(ExprNull node) { return null; }
    public R visitExprNum(ExprNum node) { return null; }
    public R visitExprSerialVal(ExprSerialVal node) { return null; }
    public R visitExprSqlRowCount(ExprSqlRowCount node) { return null; }
    public R visitExprStr(ExprStr node) { return null; }
    public R visitExprTime(ExprTime node) { return null; }
    public R visitExprTrue(ExprTrue node) { return null; }
    public R visitExprUnaryOp(ExprUnaryOp node) { return null; }
    public R visitExprZonedDateTime(ExprZonedDateTime node) { return null; }

    public R visitStmtAssign(StmtAssign node) { return null; }
    public R visitStmtBasicLoop(StmtBasicLoop node) { return null; }
    public R visitStmtBlock(StmtBlock node) { return null; }
    public R visitStmtBreak(StmtBreak node) { return null; }
    public R visitStmtCase(StmtCase node) { return null; }
    public R visitStmtCommit(StmtCommit node) { return null; }
    public R visitStmtContinue(StmtContinue node) { return null; }
    public R visitStmtCursorClose(StmtCursorClose node) { return null; }
    public R visitStmtCursorFetch(StmtCursorFetch node) { return null; }
    public R visitStmtCursorOpen(StmtCursorOpen node) { return null; }
    public R visitStmtExecImme(StmtExecImme node) { return null; }
    public R visitStmtForCursorLoop(StmtForCursorLoop node) { return null; }
    public R visitStmtForIterLoop(StmtForIterLoop node) { return null; }
    public R visitStmtForSqlLoop(StmtForSqlLoop node) { return null; }
    public R visitStmtGlobalProcCall(StmtGlobalProcCall node) { return null; }
    public R visitStmtIf(StmtIf node) { return null; }
    public R visitStmtLocalProcCall(StmtLocalProcCall node) { return null; }
    public R visitStmtNull(StmtNull node) { return null; }
    public R visitStmtOpenFor(StmtOpenFor node) { return null; }
    public R visitStmtRaise(StmtRaise node) { return null; }
    public R visitStmtRaiseAppErr(StmtRaiseAppErr node) { return null; }
    public R visitStmtReturn(StmtReturn node) { return null; }
    public R visitStmtRollback(StmtRollback node) { return null; }
    public R visitStmtSql(StmtSql node) { return null; }
    public R visitStmtWhileLoop(StmtWhileLoop node) { return null; }

    /* no need to visit the following super classes
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

    DummyExpr
    DummyStmt
     */
}

