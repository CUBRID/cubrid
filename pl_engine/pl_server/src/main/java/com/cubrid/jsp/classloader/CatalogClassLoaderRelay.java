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

import com.cubrid.jsp.code.ClassAccess;
import com.cubrid.jsp.code.CompiledCodeSet;
import com.cubrid.plcsql.compiler.visitor.JavaCodeWriter;
import java.util.HashMap;
import java.util.Map;

public class CatalogClassLoaderRelay extends ClassLoader {

    public CatalogClassLoaderRelay(long sessionId) {
        super(getSystemClassLoader());
        this.sessionId = sessionId;
    }

    public void markChildrenAsOld() {
        for (CatalogClassLoader ccl : unitClassLoaders.values()) {
            ccl.setOld(true);
        }
    }

    @Override
    public Class<?> loadClass(String name) throws ClassNotFoundException {

        // CatalogClassLoaderRelay cannot be a initiating class loader of the class of the given
        // name because
        //   . it does not call defineClass(), and
        //   . JVM does not call loadClass on it, but only the application code does.
        // This overriding is only to check the assertion.
        // TODO: remove this overridding after some time.

        assert findLoadedClass(name) == null
                : "CatalogClassLoaderRelay cannot be a initiating class loader for " + name;
        return super.loadClass(name);
    }

    public Class<?> findClassWithCompileId(String mainClassName, String compileId)
            throws ClassNotFoundException {

        // this is only called from StoredProcedure::findTargetMethod
        assert (mainClassName.startsWith("Proc_")
                || mainClassName.startsWith("Func_")
                || mainClassName.startsWith("Pckg_"));

        CatalogClassLoader classLoader = unitClassLoaders.get(mainClassName);
        if (classLoader == null) {
            classLoader = new CatalogClassLoader(mainClassName, compileId, this);
        } else {
            if (!compileId.equals(classLoader.codeSet.compileId)) {
                classLoader = new CatalogClassLoader(mainClassName, compileId, this);
                CatalogClassLoader old = unitClassLoaders.put(mainClassName, classLoader);
                old.clear();
            }
        }

        assert classLoader != null;

        // CAUTION: do not use classLoader.loadClass() :
        //  it will result in an infinite loop because classLoader's parent is this relaying class
        return classLoader.findClass(JavaCodeWriter.JAVA_PKG_OF_GENERATED + "." + mainClassName);
    }

    @Override
    public Class<?> findClass(String className) throws ClassNotFoundException {

        // control reaches here only when a stored procedure or pacakge is referenced by another,
        // and to find the class for that reference

        String mainClassName = getMainClassName(className);
        assert (mainClassName.startsWith("Proc_")
                || mainClassName.startsWith("Func_")
                || mainClassName.startsWith("Pckg_"));

        CatalogClassLoader classLoader = unitClassLoaders.get(mainClassName);
        if (classLoader == null) {
            CompiledCodeSet codeSet = ClassAccess.getObjectCodeOf(mainClassName);
            if (codeSet == null) {
                // was it dropped?
                throw new ClassNotFoundException(className);
            }
            classLoader = new CatalogClassLoader(codeSet, this);
        } else {
            if (classLoader.isOld) {
                CompiledCodeSet codeSet0 = classLoader.codeSet;
                CompiledCodeSet codeSet1 = ClassAccess.getObjectCodeNewerThan(codeSet0);
                if (codeSet1 == null) {
                    // was it dropped?
                    throw new ClassNotFoundException(className);
                } else if (codeSet1 == codeSet0) {
                    classLoader.setOld(false); // it is up-to-date
                } else {
                    classLoader = new CatalogClassLoader(codeSet1, this);
                    CatalogClassLoader old = unitClassLoaders.put(mainClassName, classLoader);
                    old.clear();
                }
            }
        }

        assert classLoader != null;

        // CAUTION: do not use classLoader.loadClass() :
        //  it will result in an infinite loop because classLoader's parent is this relaying class
        return classLoader.findClass(className);
    }

    public void clear() {
        unitClassLoaders.clear();
    }

    // =======================
    // Private
    // =======================

    private final long sessionId;
    private Map<String, CatalogClassLoader> unitClassLoaders = new HashMap<>();

    private static String getMainClassName(String className) {

        // nested class cannot reach here
        assert className.indexOf('$') == -1;
        // only pl/csql compiler generated code can reach here
        assert className.startsWith(JavaCodeWriter.JAVA_PKG_OF_GENERATED + ".");

        return className.substring(JavaCodeWriter.JAVA_PKG_OF_GENERATED.length() + 1);
    }

    private static String getInvariantPartOfMainClassName(String mainClassName) {

        // mainClassName should be of the form <invariant-part>_<seqno>_<creation-time>
        // where <invariant-part> starts with 'Proc_' or 'Func_'.

        if (!mainClassName.startsWith("Proc_") && !mainClassName.startsWith("Func_")) {
            return null;
        }

        int lastIndex = mainClassName.lastIndexOf('_');
        if (lastIndex == -1) {
            return null;
        }

        lastIndex = mainClassName.lastIndexOf('_', lastIndex - 1);
        if (lastIndex == -1) {
            return null;
        }

        return mainClassName.substring(0, lastIndex);
    }
}
