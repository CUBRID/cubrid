package com.cubrid.jsp.protocol;

import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.plcsql.compiler.SqlSemantics;
import java.util.ArrayList;
import java.util.List;

public class SqlSemanticsResponse implements UnPackableObject {

    public List<SqlSemantics> semantics = null;

    public SqlSemanticsResponse(CUBRIDUnpacker unpacker) {
        unpack(unpacker);
    }

    @Override
    public void unpack(CUBRIDUnpacker unpacker) {
        int size = unpacker.unpackInt();
        if (size > 0) {
            semantics = new ArrayList<SqlSemantics>(size);
            for (int i = 0; i < size; i++) {
                SqlSemantics s = new SqlSemantics(unpacker);
                semantics.add(s);
            }
        }
    }
}
