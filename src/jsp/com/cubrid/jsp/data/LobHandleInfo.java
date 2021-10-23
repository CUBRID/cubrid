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

import java.util.ArrayList;
import java.util.List;

public class LobHandleInfo {
    int dbType;
    long lobSize;
    byte[] lobHandle = null;
    boolean isLobLocator = false;

    /* LOB Locator */
    public LobHandleInfo (CUBRIDUnpacker unpacker) {
        dbType = unpacker.unpackInt();
        lobSize = unpacker.unpackBigint();
        
        boolean hasLob = unpacker.unpackBool();
        if (hasLob == true)
        {
            lobHandle = unpacker.unpackBytes();
        }

        isLobLocator = true;
    }

    /* LOB data */
    public LobHandleInfo (int type, byte[] data) {
        this.dbType = type;
        lobSize = data.length;
        lobHandle = data;
        isLobLocator = false;
    }

    public void pack (CUBRIDPacker packer)
    {
        packer.packInt(dbType);
        packer.packBigInt(lobSize);
        if (lobHandle == null) {
            packer.packBool(false);
        }
        else {
            packer.packBool(true);
            packer.packBytes(lobHandle);
        }
    }

    public byte[] getLobHandle() {
        return lobHandle;
    }

    public long getLobSize() {
        return lobSize;
    }

    public int getType () {
        return dbType;
    }

    public void setLobSize (long size) {
        this.lobSize = size;
    }
}
