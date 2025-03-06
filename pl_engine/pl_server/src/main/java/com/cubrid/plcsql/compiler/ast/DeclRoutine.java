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

import com.cubrid.plcsql.compiler.ast.loopOpt.SqlUse;
import com.cubrid.plcsql.compiler.ast.loopOpt.LocalRoutineCall;
import com.cubrid.plcsql.compiler.type.Type;
import org.antlr.v4.runtime.ParserRuleContext;
import java.util.Set;

public abstract class DeclRoutine extends Decl {

    public boolean calledInLoop;
    public void markCalledInLoop() {

        if (!calledInLoop) {
            calledInLoop = true;   // mark as called in at least one loop

            // recursively mark the reachable local routine calls
            for (LocalRoutineCall lrc: loopOptimizables.localRoutineCalls) {
                lrc.getDecl().markCalledInLoop();
            }
        }
    }
    public boolean isLoopOptApplicable() {
        return (loopOptimizables != null && !loopOptimizables.isEmpty());
    }
    public boolean isToGenCodeForLoopOpt() {
        return (calledInLoop && !loopOptimizables.sqlUses.isEmpty());
    }
    public void collectReachableSqlUses(Set<SqlUse> accum) {

        if (loopOptimizables != null) {
            for (SqlUse n : loopOptimizables.sqlUses) {
                boolean added = accum.add(n);
                if (!added) {
                    return; // already visited this routine declaration
                }
            }
            for (LocalRoutineCall n : loopOptimizables.localRoutineCalls) {
                n.getDecl().collectReachableSqlUses(accum);
            }
        }
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
