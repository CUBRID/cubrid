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

package com.cubrid.jsp.data;

import com.cubrid.jsp.protocol.PackableObject;
import java.util.Set;

public class CompileInfo implements PackableObject {
    public int errCode = -1; // 0: no error, < 0: error
    public int errLine = 0;
    public int errColumn = 0;
    public String errMsg = null;

    public String translated = null;
    public String createStmt = null;
    public String className = null;
    public String signature = null;

    public int compiledType = -1;
    public byte[] compiledCode = null;

    private Set<Dependency> dependencies = null;

    public CompileInfo(int code, int line, int column, String msg) {
        assert code < 0;

        errCode = code;
        errLine = line;
        errColumn = column;
        errMsg = msg;
    }

    public CompileInfo(
            String translated, String stmt, String name, String sig, Set<Dependency> dependencies) {
        errCode = 0;
        this.translated = translated;
        this.createStmt = stmt;
        this.className = name;
        this.signature = sig;
        this.dependencies = dependencies;
    }

    @Override
    public void pack(CUBRIDPacker packer) {
        packer.packInt(errCode);
        if (errCode < 0) {
            packer.packInt(errLine);
            packer.packInt(errColumn);
            packer.packString(errMsg);
        } else {
            packer.packString(translated);
            packer.packString(createStmt);
            packer.packString(className);
            packer.packString(signature);

            packer.packInt(compiledType);
            if (compiledType >= 0) {
                packer.packCString(compiledCode);
            }

            packer.packInt(dependencies.size());
            for (Dependency d : dependencies) {
                d.pack(packer);
            }
        }
    }

    public static final class Dependency implements PackableObject {

        // TODO: use server-defined enumeration
        public static final int OBJ_TYPE_TABLE = 1;
        public static final int OBJ_TYPE_VIEW = 2;
        public static final int OBJ_TYPE_FUNCTION = 3;
        public static final int OBJ_TYPE_PROCEDURE = 4;
        public static final int OBJ_TYPE_SERIAL = 5;
        public static final int OBJ_TYPE_TRIGGER = 6;
        public static final int OBJ_TYPE_SYNONYM = 7;

        int objType;
        String objUniqName;

        public Dependency(int objType, String objName, String spOwner) {
            assert objName != null;
            assert spOwner != null;

            this.objType = objType;
            if (objName.indexOf(".") < 0) {
                this.objUniqName = spOwner + "." + objName;
            } else {
                this.objUniqName = objName;
            }
        }

        @Override
        public void pack(CUBRIDPacker packer) {
            packer.packInt(objType);
            packer.packString(objUniqName);
        }

        @Override
        public boolean equals(Object o) {
            if (o == null) {
                return false;
            }
            if (o.getClass() != Dependency.class) {
                return false;
            }

            Dependency that = (Dependency) o;
            return (this.objType == that.objType) && this.objUniqName.equals(that.objUniqName);
        }

        @Override
        public int hashCode() {
            return objType + objUniqName.hashCode();
        }
    }
}
