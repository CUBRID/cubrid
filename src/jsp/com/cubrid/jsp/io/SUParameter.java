package com.cubrid.jsp.io;

public class SUParameter {
    int number;
    int types[];
    Object values[];

    SUParameter(int pNumber) {
        number = pNumber;
        types = new int[number];
        values = new Object[number];
    }
}
