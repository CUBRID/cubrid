package com.cubrid.jsp.protocol;

import com.cubrid.jsp.data.CUBRIDPacker;
import java.util.List;

public class SqlSemanticsRequest implements PackableObject {

    public List<String> sqlTexts = null;

    public SqlSemanticsRequest(List<String> sqls) {
        this.sqlTexts = sqls;
    }

    public void pack(CUBRIDPacker packer) {

        if (sqlTexts == null || sqlTexts.size() == 0) {
            throw new RuntimeException("empty SQL Semantics request");
        }

        packer.packInt(sqlTexts.size());
        for (String s : sqlTexts) {
            if (s == null || s.length() == 0) {
                throw new RuntimeException("empty SQL text");
            }
            packer.packString(s);
        }
    }
}
