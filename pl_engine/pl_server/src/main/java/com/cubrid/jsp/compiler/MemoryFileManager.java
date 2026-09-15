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

package com.cubrid.jsp.compiler;

import com.cubrid.jsp.code.CompiledCode;
import com.cubrid.plcsql.compiler.visitor.JavaCodeWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import javax.tools.FileObject;
import javax.tools.ForwardingJavaFileManager;
import javax.tools.JavaFileManager;
import javax.tools.JavaFileObject;
import javax.tools.StandardLocation;

public class MemoryFileManager extends ForwardingJavaFileManager<JavaFileManager> {

    private List<CompiledCode> codeList = new ArrayList<CompiledCode>();

    // .class files of referenced SPs/packages, injected into the CLASS_PATH so javac can resolve
    // direct calls to them
    private final List<CatalogClassFile> injectedClasses;

    protected MemoryFileManager(JavaFileManager fileManager) {
        this(fileManager, Collections.<CatalogClassFile>emptyList());
    }

    protected MemoryFileManager(
            JavaFileManager fileManager, List<CatalogClassFile> injectedClasses) {
        super(fileManager);
        this.injectedClasses = injectedClasses;
    }

    @Override
    public String inferBinaryName(JavaFileManager.Location location, JavaFileObject file) {
        if (file instanceof CatalogClassFile) {
            return ((CatalogClassFile) file).getBinaryName();
        }
        return super.inferBinaryName(location, file);
    }

    @Override
    public Iterable<JavaFileObject> list(
            JavaFileManager.Location location,
            String packageName,
            Set<JavaFileObject.Kind> kinds,
            boolean recurse)
            throws IOException {
        Iterable<JavaFileObject> superList = super.list(location, packageName, kinds, recurse);

        if (injectedClasses.isEmpty()
                || location != StandardLocation.CLASS_PATH
                || !kinds.contains(JavaFileObject.Kind.CLASS)
                || !JavaCodeWriter.JAVA_PKG_OF_GENERATED.equals(packageName)) {
            return superList;
        }

        // all injected classes belong to the single generated package
        List<JavaFileObject> merged = new ArrayList<JavaFileObject>();
        for (JavaFileObject o : superList) {
            merged.add(o);
        }
        merged.addAll(injectedClasses);
        return merged;
    }

    @Override
    public JavaFileObject getJavaFileForOutput(
            JavaFileManager.Location location,
            String className,
            JavaFileObject.Kind kind,
            FileObject sibling)
            throws IOException {
        try {
            CompiledCode c = new CompiledCode(className, false);

            // register CompiledCode in GlobalClassStore
            codeList.add(c);

            return c;
        } catch (Exception e) {
            throw new RuntimeException(
                    "Error occurs while creating output class file in memory for " + className, e);
        }
    }

    @Override
    public ClassLoader getClassLoader(JavaFileManager.Location location) {
        return null;
    }

    public List<CompiledCode> getCodeList() {
        return codeList;
    }
}
