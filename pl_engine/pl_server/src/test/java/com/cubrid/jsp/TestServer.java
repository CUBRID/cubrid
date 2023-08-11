/*
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

import static org.junit.jupiter.api.Assertions.*;

import java.nio.file.Path;
import java.util.List;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

public class TestServer {
    @AfterEach
    public void stopServer() {
        Server.stop(0);
    }

    @Test
    public void testNullArgumentsException() {
        Assertions.assertThrows(
                IllegalArgumentException.class,
                () -> {
                    Server.startWithConfig(null);
                });
    }

    @Test
    public void testNullArguments() {
        try {
            Server.startWithConfig(null);
        } catch (Exception e) {
            //
        }
        assertNull(Server.getServer());
    }

    @Test
    public void testServerJVMArguments() {
        List<String> args = Server.getJVMArguments();
        assertNotNull(args);
    }

    @Test
    public void testServerMockTCPArguments(@TempDir Path tempDir) throws Exception {
        ServerConfig config =
                new ServerConfig(
                        "mock",
                        "1.0",
                        tempDir.toAbsolutePath().toString(),
                        tempDir + "/databases",
                        "5151");
        Server.startWithConfig(config);
        assertEquals(5151, Server.getServer().getServerPort());
    }

    @Test
    public void testServerMockUDSArguments(@TempDir Path tempDir) throws Exception {
        ServerConfig config =
                new ServerConfig(
                        "mock",
                        "1.0",
                        tempDir.toAbsolutePath().toString(),
                        tempDir + "/databases",
                        tempDir + "/temp.sock");
        Server.startWithConfig(config);
        assertEquals(Server.PORT_NUMBER_UDS, Server.getServer().getServerPort());
    }
}
