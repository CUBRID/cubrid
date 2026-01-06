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

package com.cubrid.plcsql.compiler.type;

import com.cubrid.plcsql.compiler.InstanceStore;
import com.cubrid.plcsql.compiler.Misc;
import java.util.List;

public class TypeRecord extends Type {

    public final List<Misc.Pair<String, Type>> selectList;

    public boolean generateEq; // whether to generate a opEq method for this record type

    public static TypeRecord getInstance(
            InstanceStore iStore,
            boolean ofTable,
            String rowName,
            List<Misc.Pair<String, Type>> selectList) {

        TypeRecord ret = iStore.typeRecord.get(selectList);
        if (ret == null) {
            int seq = iStore.typeRecord.size();
            ret = new TypeRecord(ofTable, rowName + seq, selectList);
            iStore.typeRecord.put(selectList, ret);
        }

        return ret;
    }

    @Override
    public String toString() {
        return plcName + selectList.toString();
    }

    // ---------------------------------------------------------------------------
    // Private
    // ---------------------------------------------------------------------------

    // keys are select lists.

    private static String getPlcName(String rowName) {
        return String.format("%s%%ROWTYPE", rowName);
    }

    private static String getJavaName(boolean ofTable, String rowName) {
        return String.format("$Record_%s_%s", (ofTable ? "T" : "C"), rowName.replace('.', '_'));
    }

    private TypeRecord(boolean ofTable, String rowName, List<Misc.Pair<String, Type>> selectList) {
        super(IDX_RECORD, getPlcName(rowName), getJavaName(ofTable, rowName), null);
        this.selectList = selectList;
    }
}
