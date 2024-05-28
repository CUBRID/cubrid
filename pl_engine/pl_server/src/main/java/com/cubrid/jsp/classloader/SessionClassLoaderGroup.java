package com.cubrid.jsp.classloader;

import com.cubrid.jsp.code.CompiledCodeSet;
import java.util.HashMap;
import java.util.Map;

public class SessionClassLoaderGroup {

    Map<String, SessionClassLoader> classLoaders = null;

    public SessionClassLoaderGroup() {
        classLoaders = new HashMap<>();
    }

    public void clear() {
        classLoaders.clear();
        classLoaders = null;
    }

    public Class<?> loadClass(CompiledCodeSet code) throws ClassNotFoundException {
        String name = code.getMainClassName();
        SessionClassLoader cl = classLoaders.get(name);
        if (cl != null) {
            CompiledCodeSet loadedCode = cl.getCode();
            if (loadedCode.getTimeStamp() != code.getTimeStamp()) {
                // create new SessionClassLoader
                cl.clear();
                cl = new SessionClassLoader(code);
                classLoaders.put(name, cl);
            }
        } else {
            cl = new SessionClassLoader(code);
            classLoaders.put(name, cl);
        }

        return cl.loadClass(name);
    }
}
