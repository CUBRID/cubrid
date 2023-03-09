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

public abstract class AstVisitor<R> {

    public R visit(AstNode node) {
        return node.accept(this);
    }

    public abstract R visitUnit(Unit node);

    public abstract R visitDeclFunc(DeclFunc node);

    public abstract R visitDeclProc(DeclProc node);

    public abstract R visitDeclParamIn(DeclParamIn node);

    public abstract R visitDeclParamOut(DeclParamOut node);

    public abstract R visitDeclVar(DeclVar node);

    public abstract R visitDeclConst(DeclConst node);

    public abstract R visitDeclCursor(DeclCursor node);

    public abstract R visitDeclLabel(DeclLabel node);

    public abstract R visitDeclException(DeclException node);

    public abstract R visitExprBetween(ExprBetween node);

    public abstract R visitExprBinaryOp(ExprBinaryOp node);

    public abstract R visitExprCase(ExprCase node);

    public abstract R visitExprCond(ExprCond node);

    public abstract R visitExprCursorAttr(ExprCursorAttr node);

    public abstract R visitExprDate(ExprDate node);

    public abstract R visitExprDatetime(ExprDatetime node);

    public abstract R visitExprFalse(ExprFalse node);

    public abstract R visitExprField(ExprField node);

    public abstract R visitExprGlobalFuncCall(ExprGlobalFuncCall node);

    public abstract R visitExprId(ExprId node);

    public abstract R visitExprIn(ExprIn node);

    public abstract R visitExprLike(ExprLike node);

    public abstract R visitExprList(ExprList node);

    public abstract R visitExprLocalFuncCall(ExprLocalFuncCall node);

    public abstract R visitExprNull(ExprNull node);

    public abstract R visitExprUint(ExprUint node);

    public abstract R visitExprFloat(ExprFloat node);

    public abstract R visitExprSerialVal(ExprSerialVal node);

    public abstract R visitExprSqlRowCount(ExprSqlRowCount node);

    public abstract R visitExprStr(ExprStr node);

    public abstract R visitExprTime(ExprTime node);

    public abstract R visitExprTrue(ExprTrue node);

    public abstract R visitExprUnaryOp(ExprUnaryOp node);

    public abstract R visitExprZonedDateTime(ExprZonedDateTime node);

    public abstract R visitStmtAssign(StmtAssign node);

    public abstract R visitStmtBasicLoop(StmtBasicLoop node);

    public abstract R visitStmtBlock(StmtBlock node);

    public abstract R visitStmtBreak(StmtBreak node);

    public abstract R visitStmtCase(StmtCase node);

    public abstract R visitStmtCommit(StmtCommit node);

    public abstract R visitStmtContinue(StmtContinue node);

    public abstract R visitStmtCursorClose(StmtCursorClose node);

    public abstract R visitStmtCursorFetch(StmtCursorFetch node);

    public abstract R visitStmtCursorOpen(StmtCursorOpen node);

    public abstract R visitStmtExecImme(StmtExecImme node);

    public abstract R visitStmtStaticSql(StmtStaticSql node);

    public abstract R visitStmtForCursorLoop(StmtForCursorLoop node);

    public abstract R visitStmtForIterLoop(StmtForIterLoop node);

    public abstract R visitStmtForStaticSqlLoop(StmtForStaticSqlLoop node);

    public abstract R visitStmtForExecImmeLoop(StmtForExecImmeLoop node);

    public abstract R visitStmtGlobalProcCall(StmtGlobalProcCall node);

    public abstract R visitStmtIf(StmtIf node);

    public abstract R visitStmtLocalProcCall(StmtLocalProcCall node);

    public abstract R visitStmtNull(StmtNull node);

    public abstract R visitStmtOpenFor(StmtOpenFor node);

    public abstract R visitStmtRaise(StmtRaise node);

    public abstract R visitStmtRaiseAppErr(StmtRaiseAppErr node);

    public abstract R visitStmtReturn(StmtReturn node);

    public abstract R visitStmtRollback(StmtRollback node);

    public abstract R visitStmtWhileLoop(StmtWhileLoop node);

    public abstract R visitBody(Body node);

    public abstract R visitExHandler(ExHandler node);

    public abstract R visitExName(ExName node);

    public abstract R visitTypeSpecNumeric(TypeSpecNumeric node);

    public abstract R visitTypeSpecPercent(TypeSpecPercent node);

    public abstract R visitTypeSpecSimple(TypeSpecSimple node);

    public abstract R visitCaseExpr(CaseExpr node);

    public abstract R visitCaseStmt(CaseStmt node);

    public abstract R visitCondExpr(CondExpr node);

    public abstract R visitCondStmt(CondStmt node);

    public <E extends AstNode> R visitNodeList(NodeList<E> nodeList) {
        // default implementation is provided for NodeList
        for (E e : nodeList.nodes) {
            visit(e);
        }
        return null;
    }

    /* no need to visit the following classes
    # super classes/inteerfaces
    AstNode
    Decl
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
    ExprCast
     */
}
