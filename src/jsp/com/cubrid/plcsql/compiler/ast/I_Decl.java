package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Scope;
import com.cubrid.plcsql.compiler.Misc;

public interface I_Decl extends AstNode {
    void setScope(Scope scope);
    Scope scope();
    String typeStr();
}
