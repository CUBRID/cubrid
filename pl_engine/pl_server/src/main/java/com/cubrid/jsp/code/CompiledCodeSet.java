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

package com.cubrid.jsp.code;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.stream.Collectors;
import org.apache.commons.compress.archivers.jar.JarArchiveEntry;
import org.apache.commons.compress.archivers.jar.JarArchiveInputStream;

public class CompiledCodeSet {

    private String mainClass = null;
    private long timestamp = -1;
    private Map<String, CompiledCode> codeMap = null;

    public CompiledCodeSet(String mainClass, Collection<CompiledCode> codeList) {
        this.mainClass = mainClass;
        this.codeMap =
                codeList.stream()
                        .collect(Collectors.toMap(CompiledCode::getClassName, item -> item));
    }

    public String getMainClassName() {
        return mainClass;
    }

    public void add(CompiledCode c) {
        codeMap.put(c.getClassName(), c);
    }

    public void addAll(Collection<CompiledCode> cl) {
        Map<String, CompiledCode> map =
                cl.stream().collect(Collectors.toMap(CompiledCode::getClassName, item -> item));
        codeMap.putAll(map);
    }

    public Set<Entry<String, CompiledCode>> getCodeList() {
        return codeMap.entrySet();
    }

    public void setTimestamp(String tsString) {
        timestamp = Long.parseLong(tsString);
    }

    public void setTimestamp(long ts) {
        timestamp = ts;
    }

    public long getTimeStamp() {
        return timestamp;
    }

    public void clear() {
        mainClass = null;
        codeMap.clear();
    }

    public static CompiledCodeSet loadFromJar(String mainClass, byte[] jarString) throws Exception {

        List<CompiledCode> codeList = new ArrayList<>();

        try (final JarArchiveInputStream jarIn =
                new JarArchiveInputStream(new ByteArrayInputStream(jarString))) {
            JarArchiveEntry jarEntry;
            while ((jarEntry = jarIn.getNextEntry()) != null) {
                if (jarEntry.isDirectory()) {
                    continue;
                }
                final String key = jarEntry.getName();
                CompiledCode c = new CompiledCode(key);
                OutputStream os = c.openOutputStream();

                final long fileSize = jarEntry.getSize();
                byte[] buffer = null;
                if (fileSize > 0) {

                    buffer = new byte[(int) fileSize];
                } else {
                    ByteArrayOutputStream baos = new ByteArrayOutputStream();
                    while (true) {
                        int qwe = jarIn.read();
                        if (qwe == -1) break;
                        baos.write(qwe);
                    }
                    buffer = baos.toByteArray();
                }
                jarIn.read(buffer);
                os.write(buffer);
                // IOUtils.copy (jarIn, os, fileSize);
                codeList.add(c);
            }
        }

        return new CompiledCodeSet(mainClass, codeList);
    }
}
