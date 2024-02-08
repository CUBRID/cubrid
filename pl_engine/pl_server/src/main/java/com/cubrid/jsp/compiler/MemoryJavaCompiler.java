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

import com.cubrid.jsp.Server;
import java.nio.file.Path;
import java.util.Arrays;

import javax.tools.Diagnostic;
import javax.tools.DiagnosticCollector;
import javax.tools.JavaCompiler;
import javax.tools.JavaFileObject;
import javax.tools.ToolProvider;

public class MemoryJavaCompiler {

        private JavaCompiler compiler;
        private Iterable<String> options = null;
        private MemoryFileManager fileManager = null;

        public MemoryJavaCompiler () {
                compiler = ToolProvider.getSystemJavaCompiler();
                if (compiler == null) {
                        throw new IllegalStateException(
                                "Cannot find the system Java compiler. Check that your class path includes tools.jar");
                }

                Path cubrid_env_root = Server.getServer().getRootPath();
                useOptions ("-classpath", cubrid_env_root + "/java/pl_server.jar");

                fileManager = new MemoryFileManager(compiler.getStandardFileManager(null, null, null));
        }

        public void useOptions(String... options) {
                this.options = Arrays.asList(options);
        }
        
        public void compile (SourceCode code) {
                DiagnosticCollector<JavaFileObject> collector = new DiagnosticCollector<>();

                JavaCompiler.CompilationTask task = compiler.getTask(null, fileManager, collector, options, null, Arrays.asList(code));

                boolean result = task.call();
                if (!result || collector.getDiagnostics().size() > 0) {
                        String exceptionMsg = new String("Unable to compile the source");
			boolean hasErrors = false;
                        
			for (Diagnostic<? extends JavaFileObject> d : collector.getDiagnostics()) {
				switch (d.getKind()) {
				case NOTE:
				case MANDATORY_WARNING:
				case WARNING:
					break;
				case OTHER:
				case ERROR:
				default:
					hasErrors = true;
					break;
				}
			}
			if (hasErrors) {
				throw new RuntimeException(exceptionMsg.toString());
			}
                }
        }

        public MemoryFileManager getFileManager () {
                return fileManager;
        }
}
