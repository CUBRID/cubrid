/*
 *
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

package com.cubrid.jsp.classloader;

// import com.cubrid.jsp.code.CompiledCode;
// import com.cubrid.jsp.code.CompiledCodeSet;
// import java.util.Map.Entry;
// import java.util.UUID;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

public class CatalogClassLoaderRelay extends ClassLoader {

    public void switchChildrenToOld() {

        assert Collections.disjoint(pastTranClassLoaders.keySet(), currTranClassLoaders.keySet());

        pastTranClassLoaders.putAll(currTranClassLoaders);
        currTranClassLoaders = new HashMap<>();
    }

    public CatalogClassLoaderRelay(long sessionId) {
        super(getSystemClassLoader());
        this.sessionId = sessionId;
    }

    @Override
    public Class<?> loadClass(String name) throws ClassNotFoundException {

        // CatalogClassLoaderRelay cannot be a initiating class loader of the class of the given
        // name because
        //   . it does not call defineClass(), and
        //   . JVM does not call loadClass on it, but only the application code does.
        // This overriding is only to check the assertion. TODO: remove this overridding after some
        // time.

        assert findLoadedClass(name) == null
                : "CatalogClassLoaderRelay may not be a initiating class loader for " + name;
        return super.loadClass(name);
    }

    @Override
    public Class<?> findClass(String name) throws ClassNotFoundException {

        // NOTE: (class) name ends with a string of the form
        // _<seqno>_<creation-time>[$<nested-class-postfix>]
        // Detaching this string yields the invariant part of the main class name of an SP of the
        // form
        // Proc_<procedure-name> or Func_<function-name>.
        String mainClassName = getSpMainClassName(name); // which ends with _<seqno>_<creation-time>
        String spKey = getInvariantPartOfSpClassName(mainClassName);
        if (spKey == null) {
            // the name is not an SP class name
            throw new ClassNotFoundException(name);
        }

        CatalogClassLoader classLoader = currTranClassLoaders.get(spKey);
        if (classLoader == null) {

            CatalogClassLoader pastTranClassLoader =
                    pastTranClassLoaders.remove(spKey); // NOTE: remove. not get
            if (pastTranClassLoader != null
                    && mainClassName.equals(pastTranClassLoader.mainClassName)) {
                // the SP has not been recompiled since the last transaction. reuse the classloader
                currTranClassLoaders.put(spKey, pastTranClassLoader);
                classLoader = pastTranClassLoader;
            } else {
                // past class loader does not exist, or it got old and invalid because
                // the SP has been recompiled (and now has a different main class name)
                classLoader = new CatalogClassLoader(mainClassName, this);
                currTranClassLoaders.put(spKey, classLoader);
            }
        }

        assert classLoader != null;

        // CAUTION: do not use classLoader.loadClass() : it will result in an infinite loop
        return classLoader.findClass(name);
    }

    public void clear() {
        currTranClassLoaders.clear();
        pastTranClassLoaders.clear();
        currTranClassLoaders = null;
        pastTranClassLoaders = null;
    }

    // =======================
    // Private
    // =======================

    private final long sessionId;
    private Map<String, CatalogClassLoader> currTranClassLoaders = new HashMap<>();
    private Map<String, CatalogClassLoader> pastTranClassLoaders = new HashMap<>();

    private static String getSpMainClassName(String className) {

        int mainClassNameEnd = className.indexOf('$');
        if (mainClassNameEnd == -1) {
            return className;
        } else {
            return className.substring(0, mainClassNameEnd);
        }
    }

    private static String getInvariantPartOfSpClassName(String mainClassName) {

        // mainClassName should be of the form <invariant-part>_<seqno>_<creation-time>
        // where <invariant-part> starts with 'Proc_' or 'Func_'.
        int lastIndex = mainClassName.lastIndexOf('_');
        if (lastIndex == -1) {
            return null;
        }

        lastIndex = mainClassName.lastIndexOf('_', lastIndex - 1);
        if (lastIndex <= 5) {
            return null;
        }

        return mainClassName.substring(0, lastIndex);
    }
}
