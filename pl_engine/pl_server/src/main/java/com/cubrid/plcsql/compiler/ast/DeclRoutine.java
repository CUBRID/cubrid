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

import com.cubrid.plcsql.compiler.ast.loopOpt.LocalRoutineCall;
import com.cubrid.plcsql.compiler.ast.loopOpt.SqlUse;
import com.cubrid.plcsql.compiler.type.Type;
import java.util.Set;
import java.util.Stack;
import org.antlr.v4.runtime.ParserRuleContext;

public abstract class DeclRoutine extends Decl {

    // not contained in a loop but reachable from it including the case of (mutually) recursive
    // calls
    public boolean calledFromLoop;

    public void markAsCalledFromLoop(Set<SqlUse> accum) {

        assert loopOptimizables != null;

        if (!calledFromLoop) { // must be marked only once

            calledFromLoop = true; // mark as reachable from at least one loop

            // collect sql uses in this routine decl into the accum
            for (SqlUse n : loopOptimizables.sqlUses) {
                boolean added = accum.add(n);
                assert added; // because this routine declaration is first visited
                assert !n.reachableFromLoop();
                n.markAsReachableFromLoop();
            }

            // recursively mark the reachable local routine calls
            for (LocalRoutineCall lrc : loopOptimizables.localRoutineCalls) {
                lrc.getDecl().markAsCalledFromLoop(accum);
            }
        }
    }

    public DeclRoutine visitToFindRecursiveCalls(Set<SqlUse> accum, Stack<DeclRoutine> calls) {

        if (loopOptimizables == null) {
            return null; // TODO: self recursive routine. optimize this case too
        }

        if (calledFromLoop) {
            return null;
        }

        if (calls.indexOf(this) >= 0) {
            return this;
        }

        calls.push(this);
        int callsSize = calls.size();

        int lowestIndex = -1;
        DeclRoutine lowestRecCallHead = null;

        for (LocalRoutineCall lrc : loopOptimizables.localRoutineCalls) {

            DeclRoutine r = lrc.getDecl().visitToFindRecursiveCalls(accum, calls);
            if (r != null) {
                // this routine is on a loop of recursive calls

                if (!calledFromLoop) { // must be marked only once

                    calledFromLoop = true;

                    // collect sql uses in this routine decl into the accum
                    for (SqlUse n : loopOptimizables.sqlUses) {
                        boolean added = accum.add(n);
                        assert added; // because this routine declaration is first visited
                        assert !n.reachableFromLoop();
                        n.markAsReachableFromLoop();
                    }
                }

                int indexInCalls = calls.indexOf(r);
                if (indexInCalls < callsSize - 1) {
                    // only when the head is lower than this routine.

                    if (lowestRecCallHead == null) {
                        // never set
                        lowestRecCallHead = r;
                        lowestIndex = indexInCalls;
                    } else if (indexInCalls < lowestIndex) {
                        // lower one found
                        lowestRecCallHead = r;
                        lowestIndex = indexInCalls;
                    }
                }
            }
        }

        calls.pop();

        return lowestRecCallHead;
    }

    public boolean isNotContainedInLoop() {
        return (loopOptimizables != null);
    }

    public final String name;
    public StmtLoop.LoopOptimizables loopOptimizables;
    public final NodeList<DeclParam> paramList;
    public final TypeSpec retTypeSpec;
    public NodeList<Decl> decls;
    public Body body;

    public DeclRoutine(
            ParserRuleContext ctx,
            String name,
            StmtLoop.LoopOptimizables loopOptimizables,
            NodeList<DeclParam> paramList,
            TypeSpec retTypeSpec,
            NodeList<Decl> decls,
            Body body) {
        super(ctx);

        this.name = name;
        this.loopOptimizables = loopOptimizables;
        this.paramList = paramList;
        this.retTypeSpec = retTypeSpec;
        this.decls = decls;
        this.body = body;
    }

    public boolean hasTimestampParam() {

        if (paramList != null) {
            for (DeclParam dp : paramList.nodes) {
                if (dp.typeSpec.type == Type.TIMESTAMP) {
                    return true;
                }
            }
        }

        return false;
    }

    public String getDeclBlockName() {
        return name.toLowerCase() + '_' + (scope.level + 1);
    }

    public boolean isProcedure() {
        return (retTypeSpec == null);
    }
}
