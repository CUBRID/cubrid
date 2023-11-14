package com.cubrid.jsp.connection;

public class QueueEntry {
    private byte[] buffer;
    private long key;

    private int size;
    private int rc;
    private int transactionId;
    private int invalidateSnapshot;
    private int dbError;
    private boolean isInMethod;

    byte lock;
}
