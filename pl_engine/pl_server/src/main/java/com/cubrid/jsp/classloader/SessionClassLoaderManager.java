package com.cubrid.jsp.classloader;

import com.cubrid.jsp.code.CompiledCodeSet;
import com.cubrid.jsp.code.MemoryClass;
import java.util.HashMap;
import java.util.Map;

public class SessionClassLoaderManager {

    private long id;

    private Map<String, MemoryClass> sessionScopedLoadedCode; // <Main Class Name, MemoryClass>

    private SessionClassLoaderGroup sessionClassLoaderGroup;

    public SessionClassLoaderManager(long id) {
        this.id = id;
        sessionScopedLoadedCode = new HashMap<>();
        sessionClassLoaderGroup = new SessionClassLoaderGroup();
    }

    public Class<?> loadClass(CompiledCodeSet code) throws ClassNotFoundException {
        if (code == null) {
            return null;
        }

        String className = code.getMainClassName();
        MemoryClass mCls = null;
        if (!sessionScopedLoadedCode.containsKey(className)) {
            mCls = new MemoryClass(className);
            mCls.setCode(code);
            sessionScopedLoadedCode.put(className, mCls);
        }

        Class<?> loadedClass = sessionClassLoaderGroup.loadClass(code);
        if (mCls != null && loadedClass != null) {
            mCls.setLoadedClass(loadedClass);
        }

        return loadedClass;
    }

    public Class<?> findClass(String mainClassName) {
        MemoryClass mCls = null;
        if (sessionScopedLoadedCode.containsKey(mainClassName)) {
            mCls = sessionScopedLoadedCode.get(mainClassName);
        }

        if (mCls != null) {
            return mCls.getLoadedClass();
        }

        return null;
    }

    public void clear() {
        sessionScopedLoadedCode.clear();

        if (sessionClassLoaderGroup != null) {
            sessionClassLoaderGroup.clear();
        }
        sessionClassLoaderGroup = new SessionClassLoaderGroup();
    }

    public long getId() {
        return id;
    }
}
