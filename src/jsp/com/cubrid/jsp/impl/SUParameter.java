package com.cubrid.jsp.impl;

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
