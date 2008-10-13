package com.cubrid.jsp;

import java.util.HashMap;

public class TargetMethodCache {

    private HashMap methods;
    
    private static TargetMethodCache instance = null;
    
    private TargetMethodCache() {
        methods = new HashMap();
    }

    public TargetMethod get(String signature) throws Exception {
        TargetMethod method = null;
        
        method = (TargetMethod) methods.get(signature);
        if (method == null) {
            method = new TargetMethod(signature);
            methods.put(signature, method);
        }
        
        return method;
    }
    
    public static TargetMethodCache getInstance() {
        if (instance == null) {
            instance = new TargetMethodCache();
        }
        
        return instance;
    }
}
