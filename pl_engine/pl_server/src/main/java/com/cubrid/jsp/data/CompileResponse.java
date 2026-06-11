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
import com.cubrid.plcsql.compiler.serverapi.ServerConstants;
import java.util.LinkedList;
import java.util.List;
import java.util.Set;

public class CompileResponse implements PackableObject {

    public int errCode = -1; // 0: no error, < 0: error
    public int errLine = 0;
    public int errColumn = 0;
    public String errMsg = null;

    public int type = 0; // CompileRequest.PLCSQL_COMPILE_TYPE_...

    // common to sp and package spec
    public String translated = null;
    public String className = null;
    public byte[] compiledCode = null;
    private Set<Dependency> dependencies = null;

    // only for package spec
    public List<PkgSp> sp;
    public List<PkgVar> var;
    public List<PkgException> exception;
    public List<PkgCursor> cursor;
    public List<PkgRecType> recType;

    // only for sp
    public String javaSignature = null;
    public int sqlDataAccess = ServerConstants.SP_SQL_TYPE_UNKNOWN;

    public CompileResponse(int errCode, int line, int column, String msg) {

        assert errCode < 0;

        this.errCode = errCode;
        this.errLine = line;
        this.errColumn = column;
        this.errMsg = msg;
    }

    public void addPkgSp(PkgSp p) {
        if (sp == null) {
            sp = new LinkedList<>();
        }
        sp.add(p);
    }

    public void addPkgVar(int dbType, int prec, int scale, int flags, String name, String comment) {
        if (var == null) {
            var = new LinkedList<>();
        }
        var.add(new PkgVar(dbType, prec, scale, flags, name, comment));
    }

    public void addPkgException(String name, String comment) {
        if (exception == null) {
            exception = new LinkedList<>();
        }
        exception.add(new PkgException(name, comment));
    }

    public void addPkgCursor(
            String name, String recordType, String comment, List<String> parameters) {
        if (cursor == null) {
            cursor = new LinkedList<>();
        }
        cursor.add(new PkgCursor(name, recordType, comment, parameters));
    }

    public void addPkgRecType(String name, String comment, List<String> fields) {
        if (recType == null) {
            recType = new LinkedList<>();
        }
        recType.add(new PkgRecType(name, comment, fields));
    }

    // for Stored Procedure
    public CompileResponse(
            int type,
            String translated,
            String className,
            String javaSignature,
            int sqlDataAccess,
            Set<Dependency> dependencies) {

        assert type == CompileRequest.PLCSQL_COMPILE_TYPE_SP;

        this.errCode = 0;
        this.type = type;
        this.translated = translated;
        this.className = className;
        this.javaSignature = javaSignature;
        this.sqlDataAccess = sqlDataAccess;
        this.dependencies = dependencies;
    }

    // for Package Spec
    public CompileResponse(
            int type, String translated, String className, Set<Dependency> dependencies) {

        assert type == CompileRequest.PLCSQL_COMPILE_TYPE_PKG_SPEC;

        this.errCode = 0;
        this.type = type;
        this.translated = translated;
        this.className = className;
        this.dependencies = dependencies;
    }

    // for Package Body
    public CompileResponse(int type) {

        assert type == CompileRequest.PLCSQL_COMPILE_TYPE_PKG_BODY;

        errCode = 0;
        this.type = type;
    }

    @Override
    public void pack(CUBRIDPacker packer) {
        packer.packInt(errCode);
        if (errCode < 0) {
            packer.packInt(errLine);
            packer.packInt(errColumn);
            packer.packString(errMsg);
        } else {

            packer.packInt(type);

            switch (type) {
                case CompileRequest.PLCSQL_COMPILE_TYPE_SP:
                    packer.packString(translated);
                    packer.packString(className);
                    packer.packCString(compiledCode);
                    packer.packString(javaSignature);
                    packer.packInt(sqlDataAccess);

                    if (dependencies != null && dependencies.size() > 0) {
                        packer.packInt(dependencies.size());
                        for (Dependency d : dependencies) {
                            d.pack(packer);
                        }
                    } else {
                        packer.packInt(0);
                    }

                    break;

                case CompileRequest.PLCSQL_COMPILE_TYPE_PKG_SPEC:
                    packer.packString(translated);
                    packer.packString(className);
                    packer.packCString(compiledCode);

                    if (dependencies != null && dependencies.size() > 0) {
                        packer.packInt(dependencies.size());
                        for (Dependency d : dependencies) {
                            d.pack(packer);
                        }
                    } else {
                        packer.packInt(0);
                    }

                    if (sp != null && sp.size() > 0) {
                        packer.packInt(sp.size());
                        for (PkgSp s : sp) {
                            s.pack(packer);
                        }
                    } else {
                        packer.packInt(0);
                    }

                    if (var != null && var.size() > 0) {
                        packer.packInt(var.size());
                        for (PkgVar v : var) {
                            v.pack(packer);
                        }
                    } else {
                        packer.packInt(0);
                    }

                    if (exception != null && exception.size() > 0) {
                        packer.packInt(exception.size());
                        for (PkgException e : exception) {
                            e.pack(packer);
                        }
                    } else {
                        packer.packInt(0);
                    }

                    if (cursor != null && cursor.size() > 0) {
                        packer.packInt(cursor.size());
                        for (PkgCursor c : cursor) {
                            c.pack(packer);
                        }
                    } else {
                        packer.packInt(0);
                    }

                    if (recType != null && recType.size() > 0) {
                        packer.packInt(recType.size());
                        for (PkgRecType r : recType) {
                            r.pack(packer);
                        }
                    } else {
                        packer.packInt(0);
                    }

                    break;

                case CompileRequest.PLCSQL_COMPILE_TYPE_PKG_BODY:
                    // nothing more to pack
                    break;

                default:
                    assert false;
            }
        }
    }

    static class PkgSpArg implements PackableObject {

        public String name;
        public int dbType;
        public int mode;
        public String defaultValue;
        public String comment;

        PkgSpArg(String name, int dbType, int mode, String defaultValue, String comment) {
            this.name = name;
            this.dbType = dbType;
            this.mode = mode;
            this.defaultValue = defaultValue == null ? "" : defaultValue;
            this.comment = comment == null ? "" : comment;
        }

        @Override
        public void pack(CUBRIDPacker packer) {
            packer.packString(name);
            packer.packInt(dbType);
            packer.packInt(mode);
            packer.packString(defaultValue);
            packer.packString(comment);
        }
    }

    public static class PkgSp implements PackableObject {

        public String javaSignature;
        public String name;
        public int type;
        public int returnType;
        public int directive;
        public int sqlDataAccess;
        public String comment;
        public List<PkgSpArg> args;

        public PkgSp(
                String javaSignature,
                String name,
                int type,
                int returnType,
                int directive,
                int sqlDataAccess,
                String comment) {

            this.javaSignature = javaSignature;
            this.name = name;
            this.type = type;
            this.returnType = returnType;
            this.directive = directive;
            this.sqlDataAccess = sqlDataAccess;
            this.comment = comment == null ? "" : comment;
        }

        public void addArg(String name, int dbType, int mode, String defaultValue, String comment) {
            if (args == null) {
                args = new LinkedList<>();
            }
            args.add(new PkgSpArg(name, dbType, mode, defaultValue, comment));
        }

        @Override
        public void pack(CUBRIDPacker packer) {

            packer.packString(javaSignature);
            packer.packString(name);
            packer.packInt(type);
            packer.packInt(returnType);
            packer.packInt(directive);
            packer.packInt(sqlDataAccess);
            packer.packString(comment);

            if (args != null && args.size() > 0) {
                packer.packInt(args.size());
                for (PkgSpArg a : args) {
                    a.pack(packer);
                }
            } else {
                packer.packInt(0);
            }
        }
    }

    static class PkgVar implements PackableObject {

        public int dbType;
        public int prec;
        public int scale;
        public int flags;
        public String name;
        public String comment;

        PkgVar(int dbType, int prec, int scale, int flags, String name, String comment) {
            this.dbType = dbType;
            this.prec = prec;
            this.scale = scale;
            this.flags = flags;
            this.name = name;
            this.comment = comment == null ? "" : comment;
        }

        @Override
        public void pack(CUBRIDPacker packer) {
            packer.packInt(dbType);
            packer.packInt(prec);
            packer.packInt(scale);
            packer.packInt(flags);
            packer.packString(name);
            packer.packString(comment);
        }
    }

    static class PkgException implements PackableObject {

        public String name;
        public String comment;

        PkgException(String name, String comment) {
            this.name = name;
            this.comment = comment == null ? "" : comment;
        }

        @Override
        public void pack(CUBRIDPacker packer) {
            packer.packString(name);
            packer.packString(comment);
        }
    }

    static class PkgCursor implements PackableObject {

        public String name;
        public String recordType;
        public String comment;
        public List<String> parameters;

        PkgCursor(String name, String recordType, String comment, List<String> parameters) {
            this.name = name;
            this.recordType = recordType;
            this.comment = comment == null ? "" : comment;
            this.parameters = parameters;
        }

        @Override
        public void pack(CUBRIDPacker packer) {
            packer.packString(name);
            packer.packString(recordType);
            packer.packString(comment);
            if (parameters != null && parameters.size() > 0) {
                packer.packInt(parameters.size());
                for (String p : parameters) {
                    packer.packString(p);
                }
            } else {
                packer.packInt(0);
            }
        }
    }

    static class PkgRecType implements PackableObject {

        public String name;
        public String comment;
        public List<String> fields;

        PkgRecType(String name, String comment, List<String> fields) {
            this.name = name;
            this.comment = comment == null ? "" : comment;
            this.fields = fields;
        }

        @Override
        public void pack(CUBRIDPacker packer) {
            packer.packString(name);
            packer.packString(comment);
            if (fields != null && fields.size() > 0) {
                packer.packInt(fields.size());
                for (String f : fields) {
                    packer.packString(f);
                }
            } else {
                packer.packInt(0);
            }
        }
    }
}
