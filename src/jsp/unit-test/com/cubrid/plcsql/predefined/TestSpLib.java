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

package com.cubrid.plcsql.predefined;

import static com.cubrid.plcsql.predefined.sp.SpLib.*;

import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertFalse;

import org.junit.Test;

public class TestSpLib {
    @Test
    public void testOpNot() {
        assertTrue(opNot(false));
        assertFalse(opNot(true));
    }

    @Test
    public void testOpIsNull() {
        assertTrue(opIsNull(null));
        assertFalse(opIsNull(true));
        assertFalse(opIsNull("hello"));
    }

    @Test
    public void testOpLike() {
        assertTrue(opLike("abc", "abc", null));
        assertTrue(opLike("abc", "_bc", null));
        assertTrue(opLike("abc", "%bc", null));
        assertTrue(opLike("abc", "a%bc", null));
        assertTrue(opLike("abc", "a%", null));
        assertTrue(opLike("a%bc", "a\\%%", "\\"));
    }

    @Test
    public void testOpNeg() {
    }

    @Test
    public void testOpBitCompli() {
    }

    @Test
    public void testOpAnd() {
    }

    @Test
    public void testOpOr() {
    }

    @Test
    public void testOpXor() {
    }

    @Test
    public void testOpEq() {
    }

    @Test
    public void testOpNullSafeEq() {
    }

    @Test
    public void testOpNeq() {
    }

    @Test
    public void testOpLe() {
    }

    @Test
    public void testOpGe() {
    }

    @Test
    public void testOpLt() {
    }

    @Test
    public void testOpGt() {
    }

    @Test
    public void testOpSetEq() {
    }

    @Test
    public void testOpSetNeq() {
    }

    @Test
    public void testOpSuperset() {
    }

    @Test
    public void testOpSubset() {
    }

    @Test
    public void testOpSupersetEq() {
    }

    @Test
    public void testOpSubsetEq() {
    }

    @Test
    public void testOpBetween() {
    }

    @Test
    public void testOpIsIn() {
    }

    @Test
    public void testOpMult() {
    }

    @Test
    public void testOpDiv() {
    }

    @Test
    public void testOpDivInt() {
    }

    @Test
    public void testOpMod() {
    }

    @Test
    public void testOpAdd() {
    }

    @Test
    public void testOpSubtract() {
    }

    @Test
    public void testOpConcat() {
    }

    @Test
    public void testOpBitShiftLeft() {
    }

    @Test
    public void testOpBitShiftRight() {
    }

    @Test
    public void testOpBitAnd() {
    }

    @Test
    public void testOpBitXor() {
    }

    @Test
    public void testOpBitOr() {
    }
}

