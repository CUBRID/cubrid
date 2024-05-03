package com.cubrid.jsp.protocol;

import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.plcsql.compiler.serverapi.ServerAPI.Question;
import java.util.List;

public class GlobalSemanticsRequest implements PackableObject {

    public List<Question> questions = null;

    public GlobalSemanticsRequest(List<Question> questions) {
        this.questions = questions;
    }

    @Override
    public void pack(CUBRIDPacker packer) {
        if (questions != null) {
            packer.packBigInt(questions.size());
            for (Question q : questions) {
                q.pack(packer);
            }
        } else {
            assert false;
        }
    }
}
