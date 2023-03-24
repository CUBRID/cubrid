package com.cubrid.jsp.protocol;

import com.cubrid.jsp.data.CUBRIDPacker;
import java.util.List;

public class SqlSemanticsRequest implements PackableObject {

    public List<String> sqlTexts = null;

    public SqlSemanticsRequest(List<String> sqls) {
        this.sqlTexts = sqls;
    }

    public void pack(CUBRIDPacker packer) {
        if (sqlTexts != null) {
            packer.packInt(sqlTexts.size());
            for (String s : sqlTexts) {
                packer.packString(s);
            }
        } else {
            packer.packInt(0);
        }
    }
}
