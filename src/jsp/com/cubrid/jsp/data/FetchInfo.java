package com.cubrid.jsp.data;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.impl.SUResultTuple;

public class FetchInfo {
    public int numFetched;
    public SUResultTuple[] tuples;

    public FetchInfo (CUBRIDUnpacker unpacker) throws TypeMismatchException {
        numFetched = unpacker.unpackInt();
        if (numFetched > 0)
        {
            tuples = new SUResultTuple[numFetched];
            for (int i = 0;  i < numFetched; i++) {
                tuples[i] = new SUResultTuple (unpacker);
            }
        }
    }
}
