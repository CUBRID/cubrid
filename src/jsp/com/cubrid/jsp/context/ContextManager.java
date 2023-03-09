package com.cubrid.jsp.context;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;

public class ContextManager {

    // singleton
    private ContextManager() {
        //
    }

    // Context ID => Context Object
    private static ConcurrentMap<Long, Context> contextMap = new ConcurrentHashMap<Long, Context>();

    public static boolean hasContext(long id) {
        return contextMap.containsKey(id);
    }

    public static Context getContext(long id) {
        if (hasContext(id)) {
            return contextMap.get(id);
        } else {
            synchronized (ContextManager.class) {
                Context newCtx = new Context(id);
                contextMap.put(id, newCtx);
                return newCtx;
            }
        }
    }

    // Java Thread ID => Context ID
    private static ConcurrentMap<Long, Long> contextThreadMap = new ConcurrentHashMap<Long, Long>();

    public static void registerThread(long threadId, long ctxId) {
        if (contextThreadMap.containsKey(threadId) == false) {
            contextThreadMap.put(threadId, ctxId);
        }
    }

    public static void deregisterThread(long threadId) {
        if (contextThreadMap.containsKey(threadId) == true) {
            contextThreadMap.remove(threadId);
        }
    }

    public static Long getContextIdByThreadId(long threadId) {
        if (contextThreadMap.containsKey(threadId)) {
            return contextThreadMap.get(threadId);
        }
        return null;
    }
}
