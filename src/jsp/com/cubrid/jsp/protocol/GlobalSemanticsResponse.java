package com.cubrid.jsp.protocol;

import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.plcsql.compiler.ServerAPI.Question;
import java.util.List;

public class GlobalSemanticsResponse implements UnPackableObject {

    public List<Question> semantics = null;

    public GlobalSemanticsResponse(List<Question> semantics, CUBRIDUnpacker unpacker) {
        this.semantics = semantics;
        unpack(unpacker);
    }

    @Override
    public void unpack(CUBRIDUnpacker unpacker) {
        int size = unpacker.unpackInt();
        if (size > 0) {
            for (int i = 0; i < size; i++) {
                semantics.get(i).unpack(unpacker);
            }
        } else {
            assert false;
        }
    }

    public List<Question> getResponse() {
        return semantics;
    }
}
