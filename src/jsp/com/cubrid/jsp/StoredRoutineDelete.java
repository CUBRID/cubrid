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

package com.cubrid.jsp;

import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.List;

import com.cubrid.jsp.classloader.ClassLoaderManager;

public class StoredRoutineDelete extends SimpleFileVisitor<Path> {
    private static final Path baseDir = ClassLoaderManager.getDynamicPath();

    private String prefix = null;
    private String pattern = null;

    private List<Path> files = new ArrayList<Path>();

    public StoredRoutineDelete(String name, int type) {
        super();

        // TODO: dummy
        if (type == 2) {
            prefix = "Func_";
        } else {
            prefix = "Proc_";
        }
        pattern = prefix + name;
    }

    @Override
    public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) {
        if (attrs.isRegularFile()) {
            String name = file.getFileName().toString();
            if (name.endsWith(".java") || name.endsWith(".class")) {
                // name = name.substring(prefix.length());
                String[] splitByDollar = name.split("$");
                String rootClassName = splitByDollar[0];
                rootClassName = rootClassName.substring(0, rootClassName.indexOf('.')); // remove extension
                if (pattern.equalsIgnoreCase(rootClassName)) {
                    files.add(file);
                }
            }
        }

        return FileVisitResult.CONTINUE;
    }

    public int delete() {
        int deleteCount = 0;

        try {
            Files.walkFileTree(baseDir, this);
            if (!files.isEmpty()) {
                for (Path p : files) {
                    Files.delete(p);
                    deleteCount++;
                }
                files.clear();
            }
        } catch (Exception e) {
            deleteCount = -1;
        }

        return deleteCount;
    }
}
