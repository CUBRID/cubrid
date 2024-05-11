package com.cubrid.jsp.protocol;

import com.cubrid.jsp.data.CUBRIDUnpacker;

public interface UnPackableObject {
    void unpack(CUBRIDUnpacker packer);
}
