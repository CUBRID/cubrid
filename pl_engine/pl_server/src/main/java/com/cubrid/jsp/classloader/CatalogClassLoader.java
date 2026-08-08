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
import com.cubrid.jsp.code.CompiledCode;
import com.cubrid.jsp.code.CompiledCodeSet;
import java.util.HashMap;
import java.util.Map;

public class CatalogClassLoader extends ClassLoader {

    public final String mainClassName;

    public boolean isOld;
    public CompiledCodeSet codeSet;

    public CatalogClassLoader(String mainClassName, String compileId, ClassLoader parent) {
        super(parent);

        this.mainClassName = mainClassName;

        this.codeSet = ClassAccess.getObjectCodeOfCurrentInvoke();
        if (codeSet == null) {
            throw new IllegalStateException(
                    "retrieving object code failed for a class " + mainClassName);
        }
        this.codeSet.setMainClassName(mainClassName);
        this.codeSet.setCompileId(compileId);
    }

    public CatalogClassLoader(CompiledCodeSet codeSet, ClassLoader parent) {
        super(parent);

        this.mainClassName = codeSet.mainClassName;
        this.codeSet = codeSet;
    }

    @Override
    public Class<?> loadClass(String name) throws ClassNotFoundException {
        if (name.startsWith(mainClassName + "$")) {
            // shortcut for the nested classes of the main class
            // do not let them reach the relaying parent
            return findClass(name);
        } else {
            return super.loadClass(name);
        }
    }

    @Override
    public Class<?> findClass(String name) throws ClassNotFoundException {

        Class<?> ret = defined.get(name);
        if (ret != null) {
            return ret;
        }

        CompiledCode code = codeSet.codeMap.get(name);
        if (code == null) {
            throw new ClassNotFoundException(name);
        }

        byte[] classBytes = code.getByteCode();
        ret = defineClass(name, classBytes, 0, classBytes.length);
        defined.put(name, ret);

        return ret;
    }

    public void clear() {
        // faster garbage collection?
        codeSet.clear();
        defined.clear();
    }

    public void setOld(boolean val) {
        isOld = val;
    }

    // ===========================
    // Private
    // ===========================

    private Map<String, Class<?>> defined = new HashMap<>();
}
