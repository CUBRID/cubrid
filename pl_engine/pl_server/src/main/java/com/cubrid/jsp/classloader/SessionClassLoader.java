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

import com.cubrid.jsp.code.CompiledCode;
import com.cubrid.jsp.code.CompiledCodeSet;
import java.util.Map.Entry;
import java.util.UUID;

public class SessionClassLoader extends ClassLoader {

    private String id = null;
    private CompiledCodeSet code = null;

    public SessionClassLoader(CompiledCodeSet code) {
        id = UUID.randomUUID().toString();
        this.code = code;
    }

    public String getId() {
        return id;
    }

    public CompiledCodeSet getCode() {
        return code;
    }

    @Override
    public Class<?> loadClass(String name) throws ClassNotFoundException {
        Class<?> mainCls = findLoadedClass(name);
        if (mainCls != null) {
            // already loaded
        } else {
            try {
                mainCls = super.loadClass(name);
                if (mainCls != null) {
                    return mainCls;
                }
            } catch (ClassNotFoundException e) {
                // ignore
            }

            // find in codesets
            if (code != null) {
                for (Entry<String, CompiledCode> entry : code.getCodeList()) {
                    Class<?> cls = null;
                    String className = entry.getKey();
                    byte[] classBytes = entry.getValue().getByteCode();
                    cls = defineClass(className, classBytes, 0, classBytes.length);
                    if (name.equals(className)) {
                        mainCls = cls;
                    }
                }
            }
        }

        return mainCls;
    }

    public void clear() {
        id = null;
        code = null;
    }
}
