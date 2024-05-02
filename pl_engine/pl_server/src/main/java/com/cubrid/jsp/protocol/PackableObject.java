package com.cubrid.jsp.protocol;

import com.cubrid.jsp.data.CUBRIDPacker;

public interface PackableObject {
    void pack(CUBRIDPacker packer);
}
