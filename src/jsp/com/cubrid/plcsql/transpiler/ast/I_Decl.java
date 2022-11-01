package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Scope;
import com.cubrid.plcsql.transpiler.Misc;

public interface I_Decl extends AstNode {
    void setScope(Scope scope);
    Scope scope();
    String typeStr();
}
