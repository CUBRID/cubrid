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
import static org.junit.Assert.assertEquals;

import org.junit.Test;

import java.math.BigDecimal;

import java.time.LocalDate;
import java.time.LocalTime;
import java.time.LocalDateTime;
import java.time.ZonedDateTime;
import java.time.ZoneOffset;

import java.util.Set;
import java.util.List;
import java.util.Arrays;
import java.util.HashSet;
import java.util.ArrayList;

import org.apache.commons.collections4.MultiSet;
import org.apache.commons.collections4.multiset.HashMultiSet;

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
    public void testOpNeg() {
        assertEquals(opNeg(Short.valueOf((short)1)), Short.valueOf((short)-1));
        assertEquals(opNeg(Integer.valueOf(1)), Integer.valueOf(-1));
        assertEquals(opNeg(Long.valueOf((long)1)), Long.valueOf((long)-1));
        assertEquals(opNeg(new BigDecimal("1.1")), new BigDecimal("-1.1"));
        assertEquals(opNeg(Float.valueOf(1.1f)), Float.valueOf(-1.1f));
        assertEquals(opNeg(Double.valueOf(1.1)), Double.valueOf(-1.1));
    }

    @Test
    public void testOpBitCompli() {
        assertEquals(opBitCompli(Short.valueOf((short)1)), Long.valueOf(~(long)1));
        assertEquals(opBitCompli(Integer.valueOf(1)), Long.valueOf(~(long)1));
        assertEquals(opBitCompli(Long.valueOf((long)1)), Long.valueOf(~(long)1));
    }

    @Test
    public void testOpAnd() {
        assertEquals(opAnd(true, true), true);
        assertEquals(opAnd(true, false), false);
        assertEquals(opAnd(false, true), false);
        assertEquals(opAnd(false, false), false);
    }

    @Test
    public void testOpOr() {
        assertEquals(opOr(true, true), true);
        assertEquals(opOr(true, false), true);
        assertEquals(opOr(false, true), true);
        assertEquals(opOr(false, false), false);
    }

    @Test
    public void testOpXor() {
        assertEquals(opXor(true, true), false);
        assertEquals(opXor(true, false), true);
        assertEquals(opXor(false, true), true);
        assertEquals(opXor(false, false), false);
    }

    @Test
    public void testOpEq() {
        assertEquals(opEq(1, 1), true);
        assertEquals(opEq(1, 2), false);
    }

    @Test
    public void testOpNullSafeEq() {
        assertEquals(opNullSafeEq(1, 1), true);
        assertEquals(opNullSafeEq(1, 2), false);
        assertEquals(opNullSafeEq(1, null), false);
        assertEquals(opNullSafeEq(null, null), true);
    }

    @Test
    public void testOpNeq() {
        assertEquals(opNeq(1, 1), false);
        assertEquals(opNeq(1, 2), true);
    }

    @Test
    public void testOpLe() {
        // strings
        assertEquals(opLe("abc", "abc"), true);
        assertEquals(opLe("abb", "abc"), true);
        assertEquals(opLe("abd", "abc"), false);

        // shorts
        assertEquals(opLe((short)1, (short)1), true);
        assertEquals(opLe((short)0, (short)1), true);
        assertEquals(opLe((short)2, (short)1), false);

        // int
        assertEquals(opLe(1, 1), true);
        assertEquals(opLe(0, 1), true);
        assertEquals(opLe(2, 1), false);

        // longs
        assertEquals(opLe((long)1, (long)1), true);
        assertEquals(opLe((long)0, (long)1), true);
        assertEquals(opLe((long)2, (long)1), false);

        // BigDecimals
        assertEquals(opLe(new BigDecimal("1.1"), new BigDecimal("1.1")), true);
        assertEquals(opLe(new BigDecimal("0.1"), new BigDecimal("1.1")), true);
        assertEquals(opLe(new BigDecimal("2.1"), new BigDecimal("1.1")), false);

        // floats
        assertEquals(opLe(1.1f, 1.1f), true);
        assertEquals(opLe(0.1f, 1.1f), true);
        assertEquals(opLe(2.1f, 1.1f), false);

        // doubles
        assertEquals(opLe(1.1, 1.1), true);
        assertEquals(opLe(0.1, 1.1), true);
        assertEquals(opLe(2.1, 1.1), false);

        // LocalDates
        assertEquals(opLe(LocalDate.of(2000,10,10), LocalDate.of(2000,10,10)), true);
        assertEquals(opLe(LocalDate.of(2000,10,9), LocalDate.of(2000,10,10)), true);
        assertEquals(opLe(LocalDate.of(2000,10,11), LocalDate.of(2000,10,10)), false);

        // LocalTimes
        assertEquals(opLe(LocalTime.of(10,10,10), LocalTime.of(10,10,10)), true);
        assertEquals(opLe(LocalTime.of(10,10,9), LocalTime.of(10,10,10)), true);
        assertEquals(opLe(LocalTime.of(10,10,11), LocalTime.of(10,10,10)), false);

        // LocalDateTimes
        assertEquals(opLe(LocalDateTime.of(2000,10,10,10,10,10), LocalDateTime.of(2000,10,10,10,10,10)), true);
        assertEquals(opLe(LocalDateTime.of(2000,10,9,10,10,10), LocalDateTime.of(2000,10,10,10,10,10)), true);
        assertEquals(opLe(LocalDateTime.of(2000,10,11,10,10,10), LocalDateTime.of(2000,10,10,10,10,10)), false);

        // ZonedDateTimes
        assertEquals(opLe(ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC)), true);
        assertEquals(opLe(ZonedDateTime.of(2000,10,9,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC)), true);
        assertEquals(opLe(ZonedDateTime.of(2000,10,11,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC)), false);
    }

    @Test
    public void testOpGe() {
        // strings
        assertEquals(opGe("abc", "abc"), true);
        assertEquals(opGe("abb", "abc"), false);
        assertEquals(opGe("abd", "abc"), true);

        // shorts
        assertEquals(opGe((short)1, (short)1), true);
        assertEquals(opGe((short)0, (short)1), false);
        assertEquals(opGe((short)2, (short)1), true);

        // int
        assertEquals(opGe(1, 1), true);
        assertEquals(opGe(0, 1), false);
        assertEquals(opGe(2, 1), true);

        // longs
        assertEquals(opGe((long)1, (long)1), true);
        assertEquals(opGe((long)0, (long)1), false);
        assertEquals(opGe((long)2, (long)1), true);

        // BigDecimals
        assertEquals(opGe(new BigDecimal("1.1"), new BigDecimal("1.1")), true);
        assertEquals(opGe(new BigDecimal("0.1"), new BigDecimal("1.1")), false);
        assertEquals(opGe(new BigDecimal("2.1"), new BigDecimal("1.1")), true);

        // floats
        assertEquals(opGe(1.1f, 1.1f), true);
        assertEquals(opGe(0.1f, 1.1f), false);
        assertEquals(opGe(2.1f, 1.1f), true);

        // doubles
        assertEquals(opGe(1.1, 1.1), true);
        assertEquals(opGe(0.1, 1.1), false);
        assertEquals(opGe(2.1, 1.1), true);

        // LocalDates
        assertEquals(opGe(LocalDate.of(2000,10,10), LocalDate.of(2000,10,10)), true);
        assertEquals(opGe(LocalDate.of(2000,10,9), LocalDate.of(2000,10,10)), false);
        assertEquals(opGe(LocalDate.of(2000,10,11), LocalDate.of(2000,10,10)), true);

        // LocalTimes
        assertEquals(opGe(LocalTime.of(10,10,10), LocalTime.of(10,10,10)), true);
        assertEquals(opGe(LocalTime.of(10,10,9), LocalTime.of(10,10,10)), false);
        assertEquals(opGe(LocalTime.of(10,10,11), LocalTime.of(10,10,10)), true);

        // LocalDateTimes
        assertEquals(opGe(LocalDateTime.of(2000,10,10,10,10,10), LocalDateTime.of(2000,10,10,10,10,10)), true);
        assertEquals(opGe(LocalDateTime.of(2000,10,9,10,10,10), LocalDateTime.of(2000,10,10,10,10,10)), false);
        assertEquals(opGe(LocalDateTime.of(2000,10,11,10,10,10), LocalDateTime.of(2000,10,10,10,10,10)), true);

        // ZonedDateTimes
        assertEquals(opGe(ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC)), true);
        assertEquals(opGe(ZonedDateTime.of(2000,10,9,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC)), false);
        assertEquals(opGe(ZonedDateTime.of(2000,10,11,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC)), true);
    }

    @Test
    public void testOpLt() {
        // strings
        assertEquals(opLt("abc", "abc"), false);
        assertEquals(opLt("abb", "abc"), true);
        assertEquals(opLt("abd", "abc"), false);

        // shorts
        assertEquals(opLt((short)1, (short)1), false);
        assertEquals(opLt((short)0, (short)1), true);
        assertEquals(opLt((short)2, (short)1), false);

        // int
        assertEquals(opLt(1, 1), false);
        assertEquals(opLt(0, 1), true);
        assertEquals(opLt(2, 1), false);

        // longs
        assertEquals(opLt((long)1, (long)1), false);
        assertEquals(opLt((long)0, (long)1), true);
        assertEquals(opLt((long)2, (long)1), false);

        // BigDecimals
        assertEquals(opLt(new BigDecimal("1.1"), new BigDecimal("1.1")), false);
        assertEquals(opLt(new BigDecimal("0.1"), new BigDecimal("1.1")), true);
        assertEquals(opLt(new BigDecimal("2.1"), new BigDecimal("1.1")), false);

        // floats
        assertEquals(opLt(1.1f, 1.1f), false);
        assertEquals(opLt(0.1f, 1.1f), true);
        assertEquals(opLt(2.1f, 1.1f), false);

        // doubles
        assertEquals(opLt(1.1, 1.1), false);
        assertEquals(opLt(0.1, 1.1), true);
        assertEquals(opLt(2.1, 1.1), false);

        // LocalDates
        assertEquals(opLt(LocalDate.of(2000,10,10), LocalDate.of(2000,10,10)), false);
        assertEquals(opLt(LocalDate.of(2000,10,9), LocalDate.of(2000,10,10)), true);
        assertEquals(opLt(LocalDate.of(2000,10,11), LocalDate.of(2000,10,10)), false);

        // LocalTimes
        assertEquals(opLt(LocalTime.of(10,10,10), LocalTime.of(10,10,10)), false);
        assertEquals(opLt(LocalTime.of(10,10,9), LocalTime.of(10,10,10)), true);
        assertEquals(opLt(LocalTime.of(10,10,11), LocalTime.of(10,10,10)), false);

        // LocalDateTimes
        assertEquals(opLt(LocalDateTime.of(2000,10,10,10,10,10), LocalDateTime.of(2000,10,10,10,10,10)), false);
        assertEquals(opLt(LocalDateTime.of(2000,10,9,10,10,10), LocalDateTime.of(2000,10,10,10,10,10)), true);
        assertEquals(opLt(LocalDateTime.of(2000,10,11,10,10,10), LocalDateTime.of(2000,10,10,10,10,10)), false);

        // ZonedDateTimes
        assertEquals(opLt(ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC)), false);
        assertEquals(opLt(ZonedDateTime.of(2000,10,9,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC)), true);
        assertEquals(opLt(ZonedDateTime.of(2000,10,11,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC)), false);
    }

    @Test
    public void testOpGt() {
        // strings
        assertEquals(opGt("abc", "abc"), false);
        assertEquals(opGt("abb", "abc"), false);
        assertEquals(opGt("abd", "abc"), true);

        // shorts
        assertEquals(opGt((short)1, (short)1), false);
        assertEquals(opGt((short)0, (short)1), false);
        assertEquals(opGt((short)2, (short)1), true);

        // int
        assertEquals(opGt(1, 1), false);
        assertEquals(opGt(0, 1), false);
        assertEquals(opGt(2, 1), true);

        // longs
        assertEquals(opGt((long)1, (long)1), false);
        assertEquals(opGt((long)0, (long)1), false);
        assertEquals(opGt((long)2, (long)1), true);

        // BigDecimals
        assertEquals(opGt(new BigDecimal("1.1"), new BigDecimal("1.1")), false);
        assertEquals(opGt(new BigDecimal("0.1"), new BigDecimal("1.1")), false);
        assertEquals(opGt(new BigDecimal("2.1"), new BigDecimal("1.1")), true);

        // floats
        assertEquals(opGt(1.1f, 1.1f), false);
        assertEquals(opGt(0.1f, 1.1f), false);
        assertEquals(opGt(2.1f, 1.1f), true);

        // doubles
        assertEquals(opGt(1.1, 1.1), false);
        assertEquals(opGt(0.1, 1.1), false);
        assertEquals(opGt(2.1, 1.1), true);

        // LocalDates
        assertEquals(opGt(LocalDate.of(2000,10,10), LocalDate.of(2000,10,10)), false);
        assertEquals(opGt(LocalDate.of(2000,10,9), LocalDate.of(2000,10,10)), false);
        assertEquals(opGt(LocalDate.of(2000,10,11), LocalDate.of(2000,10,10)), true);

        // LocalTimes
        assertEquals(opGt(LocalTime.of(10,10,10), LocalTime.of(10,10,10)), false);
        assertEquals(opGt(LocalTime.of(10,10,9), LocalTime.of(10,10,10)), false);
        assertEquals(opGt(LocalTime.of(10,10,11), LocalTime.of(10,10,10)), true);

        // LocalDateTimes
        assertEquals(opGt(LocalDateTime.of(2000,10,10,10,10,10), LocalDateTime.of(2000,10,10,10,10,10)), false);
        assertEquals(opGt(LocalDateTime.of(2000,10,9,10,10,10), LocalDateTime.of(2000,10,10,10,10,10)), false);
        assertEquals(opGt(LocalDateTime.of(2000,10,11,10,10,10), LocalDateTime.of(2000,10,10,10,10,10)), true);

        // ZonedDateTimes
        assertEquals(opGt(ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC)), false);
        assertEquals(opGt(ZonedDateTime.of(2000,10,9,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC)), false);
        assertEquals(opGt(ZonedDateTime.of(2000,10,11,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC)), true);
    }

    List l0 = Arrays.asList(1,2,3);
    List l00 = Arrays.asList(1,2,3);
    List l1 = Arrays.asList(1,1,2,2,3,3);
    List l2 = Arrays.asList(2,3,4);
    List l3 = Arrays.asList(1,2,3,5);

    Set s0 = new HashSet(l0);
    Set s00 = new HashSet(l00);
    Set s1 = new HashSet(l1);
    Set s2 = new HashSet(l2);
    Set s3 = new HashSet(l3);

    MultiSet m0 = new HashMultiSet(l0);
    MultiSet m00 = new HashMultiSet(l00);
    MultiSet m1 = new HashMultiSet(l1);
    MultiSet m2 = new HashMultiSet(l2);
    MultiSet m3 = new HashMultiSet(l3);

    @Test
    public void testOpSetEq() {
        assertEquals(opSetEq(s0, s1), true);
        assertEquals(opSetEq(s0, s2), false);

        assertEquals(opSetEq(m0, s1), true);
        assertEquals(opSetEq(m0, s2), false);

        assertEquals(opSetEq(l0, s1), true);
        assertEquals(opSetEq(l0, s2), false);

        assertEquals(opSetEq(s0, m0), true);
        assertEquals(opSetEq(s0, m2), false);

        assertEquals(opSetEq(m00, m0), true);
        assertEquals(opSetEq(m00, m2), false);

        assertEquals(opSetEq(l0, m0), true);
        assertEquals(opSetEq(l0, m2), false);

        assertEquals(opSetEq(s0, l0), true);
        assertEquals(opSetEq(s0, l2), false);

        assertEquals(opSetEq(m0, l0), true);
        assertEquals(opSetEq(m0, l2), false);

        assertEquals(opSetEq(l0, l00), true);
        assertEquals(opSetEq(l0, l2), false);
    }

    @Test
    public void testOpSetNeq() {
        assertEquals(opSetNeq(s0, s1), false);
        assertEquals(opSetNeq(s0, s2), true);

        assertEquals(opSetNeq(m0, s1), false);
        assertEquals(opSetNeq(m0, s2), true);

        assertEquals(opSetNeq(l0, s1), false);
        assertEquals(opSetNeq(l0, s2), true);

        assertEquals(opSetNeq(s0, m0), false);
        assertEquals(opSetNeq(s0, m2), true);

        assertEquals(opSetNeq(m00, m0), false);
        assertEquals(opSetNeq(m00, m2), true);

        assertEquals(opSetNeq(l0, m0), false);
        assertEquals(opSetNeq(l0, m2), true);

        assertEquals(opSetNeq(s0, l0), false);
        assertEquals(opSetNeq(s0, l2), true);

        assertEquals(opSetNeq(m0, l0), false);
        assertEquals(opSetNeq(m0, l2), true);

        assertEquals(opSetNeq(l0, l00), false);
        assertEquals(opSetNeq(l0, l2), true);
    }

    @Test
    public void testOpSuperset() {
        assertEquals(opSuperset(s3, s0), true);
        assertEquals(opSuperset(s3, s2), false);
        assertEquals(opSuperset(s00, s0), false);

        assertEquals(opSuperset(m3, s0), true);
        assertEquals(opSuperset(m1, s0), true);
        assertEquals(opSuperset(m3, s2), false);
        assertEquals(opSuperset(m0, s0), false);

        assertEquals(opSuperset(l3, s0), true);
        assertEquals(opSuperset(l1, s0), true);
        assertEquals(opSuperset(l3, s2), false);
        assertEquals(opSuperset(l0, s0), false);

        assertEquals(opSuperset(s3, m0), true);
        assertEquals(opSuperset(s3, m2), false);
        assertEquals(opSuperset(s0, m0), false);

        assertEquals(opSuperset(m3, m0), true);
        assertEquals(opSuperset(m1, m0), true);
        assertEquals(opSuperset(m3, m2), false);
        assertEquals(opSuperset(m00, m0), false);

        assertEquals(opSuperset(l3, m0), true);
        assertEquals(opSuperset(l1, m0), true);
        assertEquals(opSuperset(l3, m2), false);
        assertEquals(opSuperset(l0, m0), false);

        assertEquals(opSuperset(s3, l0), true);
        assertEquals(opSuperset(s3, l2), false);
        assertEquals(opSuperset(s0, l0), false);

        assertEquals(opSuperset(m3, l0), true);
        assertEquals(opSuperset(m1, l0), true);
        assertEquals(opSuperset(m3, l2), false);
        assertEquals(opSuperset(m0, l0), false);
    }

    @Test
    public void testOpSubset() {
        assertEquals(opSubset(s0, s3), true);
        assertEquals(opSubset(s0, s2), false);
        assertEquals(opSubset(s00, s0), false);

        assertEquals(opSubset(m0, s3), true);
        assertEquals(opSubset(m0, s2), false);
        assertEquals(opSubset(m0, s0), false);

        assertEquals(opSubset(l0, s3), true);
        assertEquals(opSubset(l0, s2), false);
        assertEquals(opSubset(l0, s0), false);

        assertEquals(opSubset(s0, m3), true);
        assertEquals(opSubset(s0, m1), true);
        assertEquals(opSubset(s0, m2), false);
        assertEquals(opSubset(s00, m0), false);

        assertEquals(opSubset(m0, m3), true);
        assertEquals(opSubset(m0, m1), true);
        assertEquals(opSubset(m0, m2), false);
        assertEquals(opSubset(m00, m0), false);

        assertEquals(opSubset(l0, m3), true);
        assertEquals(opSubset(l0, m1), true);
        assertEquals(opSubset(l0, m2), false);
        assertEquals(opSubset(l0, m0), false);

        assertEquals(opSubset(s0, l3), true);
        assertEquals(opSubset(s0, l1), true);
        assertEquals(opSubset(s0, l2), false);
        assertEquals(opSubset(s0, l0), false);

        assertEquals(opSubset(m0, l3), true);
        assertEquals(opSubset(m0, l1), true);
        assertEquals(opSubset(m0, l2), false);
        assertEquals(opSubset(m0, l0), false);
    }

    @Test
    public void testOpSupersetEq() {
        assertEquals(opSupersetEq(s3, s0), true);
        assertEquals(opSupersetEq(s3, s2), false);
        assertEquals(opSupersetEq(s00, s0), true);

        assertEquals(opSupersetEq(m3, s0), true);
        assertEquals(opSupersetEq(m1, s0), true);
        assertEquals(opSupersetEq(m3, s2), false);
        assertEquals(opSupersetEq(m0, s0), true);

        assertEquals(opSupersetEq(l3, s0), true);
        assertEquals(opSupersetEq(l1, s0), true);
        assertEquals(opSupersetEq(l3, s2), false);
        assertEquals(opSupersetEq(l0, s0), true);

        assertEquals(opSupersetEq(s3, m0), true);
        assertEquals(opSupersetEq(s3, m2), false);
        assertEquals(opSupersetEq(s0, m0), true);

        assertEquals(opSupersetEq(m3, m0), true);
        assertEquals(opSupersetEq(m1, m0), true);
        assertEquals(opSupersetEq(m3, m2), false);
        assertEquals(opSupersetEq(m00, m0), true);

        assertEquals(opSupersetEq(l3, m0), true);
        assertEquals(opSupersetEq(l1, m0), true);
        assertEquals(opSupersetEq(l3, m2), false);
        assertEquals(opSupersetEq(l0, m0), true);

        assertEquals(opSupersetEq(s3, l0), true);
        assertEquals(opSupersetEq(s3, l2), false);
        assertEquals(opSupersetEq(s0, l0), true);

        assertEquals(opSupersetEq(m3, l0), true);
        assertEquals(opSupersetEq(m1, l0), true);
        assertEquals(opSupersetEq(m3, l2), false);
        assertEquals(opSupersetEq(m0, l0), true);
    }

    @Test
    public void testOpSubsetEq() {
        assertEquals(opSubsetEq(s0, s3), true);
        assertEquals(opSubsetEq(s0, s2), false);
        assertEquals(opSubsetEq(s00, s0), true);

        assertEquals(opSubsetEq(m0, s3), true);
        assertEquals(opSubsetEq(m0, s2), false);
        assertEquals(opSubsetEq(m0, s0), true);

        assertEquals(opSubsetEq(l0, s3), true);
        assertEquals(opSubsetEq(l0, s2), false);
        assertEquals(opSubsetEq(l0, s0), true);

        assertEquals(opSubsetEq(s0, m3), true);
        assertEquals(opSubsetEq(s0, m1), true);
        assertEquals(opSubsetEq(s0, m2), false);
        assertEquals(opSubsetEq(s00, m0), true);

        assertEquals(opSubsetEq(m0, m3), true);
        assertEquals(opSubsetEq(m0, m1), true);
        assertEquals(opSubsetEq(m0, m2), false);
        assertEquals(opSubsetEq(m00, m0), true);

        assertEquals(opSubsetEq(l0, m3), true);
        assertEquals(opSubsetEq(l0, m1), true);
        assertEquals(opSubsetEq(l0, m2), false);
        assertEquals(opSubsetEq(l0, m0), true);

        assertEquals(opSubsetEq(s0, l3), true);
        assertEquals(opSubsetEq(s0, l1), true);
        assertEquals(opSubsetEq(s0, l2), false);
        assertEquals(opSubsetEq(s0, l0), true);

        assertEquals(opSubsetEq(m0, l3), true);
        assertEquals(opSubsetEq(m0, l1), true);
        assertEquals(opSubsetEq(m0, l2), false);
        assertEquals(opSubsetEq(m0, l0), true);
    }

    @Test
    public void testOpBetween() {
        // strings
        assertEquals(opBetween("abc", "abb", "abd"), true);
        assertEquals(opBetween("abe", "abb", "abd"), false);

        // shorts
        assertEquals(opBetween((short)1, (short)0, (short)2), true);
        assertEquals(opBetween((short)3, (short)0, (short)2), false);

        // int
        assertEquals(opBetween(1, 0, 2), true);
        assertEquals(opBetween(3, 0, 2), false);

        // longs
        assertEquals(opBetween((long)1, (long)0, (long)2), true);
        assertEquals(opBetween((long)3, (long)0, (long)2), false);

        // BigDecimals
        assertEquals(opBetween(new BigDecimal("1.1"), new BigDecimal("0.1"), new BigDecimal("2.1")), true);
        assertEquals(opBetween(new BigDecimal("3.1"), new BigDecimal("0.1"), new BigDecimal("2.1")), false);

        // floats
        assertEquals(opBetween(1.1f, 0.1f, 2.1f), true);
        assertEquals(opBetween(3.1f, 0.1f, 2.1f), false);

        // doubles
        assertEquals(opBetween(1.1, 0.1, 2.1), true);
        assertEquals(opBetween(3.1, 0.1, 2.1), false);

        // LocalDates
        assertEquals(opBetween(LocalDate.of(2000,10,11), LocalDate.of(2000,10,10), LocalDate.of(2000,10,12)), true);
        assertEquals(opBetween(LocalDate.of(2000,10,13), LocalDate.of(2000,10,10), LocalDate.of(2000,10,12)), false);

        // LocalTimes
        assertEquals(opBetween(LocalTime.of(10,10,11), LocalTime.of(10,10,10), LocalTime.of(10,10,12)), true);
        assertEquals(opBetween(LocalTime.of(10,10,13), LocalTime.of(10,10,10), LocalTime.of(10,10,12)), false);

        // LocalDateTimes
        assertEquals(opBetween(LocalDateTime.of(2000,10,10,10,10,11),
                          LocalDateTime.of(2000,10,10,10,10,10),
                          LocalDateTime.of(2000,10,10,10,10,12)), true);
        assertEquals(opBetween(LocalDateTime.of(2000,10,10,10,10,13),
                          LocalDateTime.of(2000,10,10,10,10,10),
                          LocalDateTime.of(2000,10,10,10,10,12)), false);

        // ZonedDateTimes
        assertEquals(opBetween(ZonedDateTime.of(2000,10,10,10,10,11,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,12,0, ZoneOffset.UTC)), true);
        assertEquals(opBetween(ZonedDateTime.of(2000,10,10,10,10,13,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC),
                          ZonedDateTime.of(2000,10,10,10,10,12,0, ZoneOffset.UTC)), false);

    }

    @Test
    public void testOpIn() {
        assertEquals(opIn(1, 1, 2, 3), true);
        assertEquals(opIn(5, 1, 2, 3), false);
    }

    @Test
    public void testOpMult() {
        assertEquals(opMult((short)1, (short)2), Short.valueOf((short)2));
        assertEquals(opMult(1, 2), Integer.valueOf(2));
        assertEquals(opMult((long)1, (long)2), Long.valueOf((long)2));
        assertEquals(opMult(new BigDecimal("1.1"), new BigDecimal("2.2")), new BigDecimal("2.42"));
        assertTrue(opMult(1.1f, 2.2f) != null);
        assertTrue(opMult(1.1, 2.2) != null);

        assertEquals(opMult(s0, s2), new HashSet(Arrays.asList(2,3)));
        assertEquals(opMult(m1, s2), new HashMultiSet(Arrays.asList(2,3)));
        assertEquals(opMult(l1, s2), new HashMultiSet(Arrays.asList(2,3)));

        assertEquals(opMult(s0, m2), new HashMultiSet(Arrays.asList(2,3)));
        assertEquals(opMult(m1, m2), new HashMultiSet(Arrays.asList(2,3)));
        assertEquals(opMult(l1, m2), new HashMultiSet(Arrays.asList(2,3)));

        assertEquals(opMult(s0, l2), new HashMultiSet(Arrays.asList(2,3)));
        assertEquals(opMult(m1, l2), new HashMultiSet(Arrays.asList(2,3)));
        assertEquals(opMult(l1, l2), new HashMultiSet(Arrays.asList(2,3)));
    }

    @Test
    public void testOpDiv() {
        assertEquals(opDiv((short)1, (short)2), Short.valueOf((short)0));
        assertEquals(opDiv(1, 2), Integer.valueOf(0));
        assertEquals(opDiv((long)1, (long)2), Long.valueOf((long)0));
        assertEquals(opDiv(new BigDecimal("1.1"), new BigDecimal("2.2")), new BigDecimal("0.5"));
        assertTrue(opDiv(1.1f, 2.2f) != null);
        assertTrue(opDiv(1.1, 2.2) != null);
    }

    @Test
    public void testOpDivInt() {
        assertEquals(opDivInt((short)1, (short)2), Short.valueOf((short)0));
        assertEquals(opDivInt(1, 2), Integer.valueOf(0));
        assertEquals(opDivInt((long)1, (long)2), Long.valueOf((long)0));
    }

    @Test
    public void testOpMod() {
        assertEquals(opMod((short)1, (short)2), Short.valueOf((short)1));
        assertEquals(opMod(1, 2), Integer.valueOf(1));
        assertEquals(opMod((long)1, (long)2), Long.valueOf((long)1));
    }

    @Test
    public void testOpAdd() {
        assertEquals(opAdd((short)1, (short)2), Short.valueOf((short)3));
        assertEquals(opAdd(1, 2), Integer.valueOf(3));
        assertEquals(opAdd((long)1, (long)2), Long.valueOf((long)3));
        assertEquals(opAdd(new BigDecimal("1.1"), new BigDecimal("2.2")), new BigDecimal("3.3"));
        assertTrue(opAdd(1.1f, 2.2f) != null);
        assertTrue(opAdd(1.1, 2.2) != null);

        assertEquals(opAdd(LocalTime.of(10,10,10), 10), LocalTime.of(10,10,20));
        assertEquals(opAdd(LocalDate.of(2000,10,10), 10), LocalDate.of(2000,10,20));
        assertEquals(opAdd(LocalDateTime.of(2000,10,10,10,10,10), 10), LocalDateTime.of(2000,10,10,10,10,10,10000000));
        assertEquals(opAdd(ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC), 10),
                           ZonedDateTime.of(2000,10,10,10,10,20,0, ZoneOffset.UTC));

        assertEquals(opAdd(s0, s2), new HashSet(Arrays.asList(1,2,3,4)));
        assertEquals(opAdd(m1, s2), new HashMultiSet(Arrays.asList(1,1,2,2,3,3,2,3,4)));
        assertEquals(opAdd(l1, s2), new HashMultiSet(Arrays.asList(1,1,2,2,3,3,2,3,4)));

        assertEquals(opAdd(s0, m2), new HashMultiSet(Arrays.asList(1,2,3,2,3,4)));
        assertEquals(opAdd(m1, m2), new HashMultiSet(Arrays.asList(1,1,2,2,3,3,2,3,4)));
        assertEquals(opAdd(l1, m2), new HashMultiSet(Arrays.asList(1,1,2,2,3,3,2,3,4)));

        assertEquals(opAdd(s0, l2), new HashMultiSet(Arrays.asList(1,2,3,2,3,4)));
        assertEquals(opAdd(m1, l2), new HashMultiSet(Arrays.asList(1,1,2,2,3,3,2,3,4)));
        assertEquals(opAdd(l1, l2), new ArrayList(Arrays.asList(1,1,2,2,3,3,2,3,4)));
    }

    @Test
    public void testOpSubtract() {
        assertEquals(opSubtract((short)1, (short)2), Short.valueOf((short)-1));
        assertEquals(opSubtract(1, 2), Integer.valueOf(-1));
        assertEquals(opSubtract((long)1, (long)2), Long.valueOf((long)-1));
        assertEquals(opSubtract(new BigDecimal("1.1"), new BigDecimal("2.2")), new BigDecimal("-1.1"));
        assertTrue(opSubtract(1.1f, 2.2f) != null);
        assertTrue(opSubtract(1.1, 2.2) != null);

        assertEquals(opSubtract(LocalTime.of(10,10,10), LocalTime.of(10,10,0)), Long.valueOf(10L));
        assertEquals(opSubtract(LocalDate.of(2000,10,10), LocalDate.of(2000,9,30)), Long.valueOf(10L));
        assertEquals(opSubtract(LocalDateTime.of(2000,10,10,10,10,10), LocalDateTime.of(2000,10,10,10,10,9,990000000)),
                     Long.valueOf(10L));
        assertEquals(opSubtract(ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC),
                                ZonedDateTime.of(2000,10,10,10,10,0,0, ZoneOffset.UTC)),
                     Long.valueOf(10L));

        assertEquals(opSubtract(LocalTime.of(10,10,10), 10), LocalTime.of(10,10,0));
        assertEquals(opSubtract(LocalDate.of(2000,10,10), 10), LocalDate.of(2000,9,30));
        assertEquals(opSubtract(LocalDateTime.of(2000,10,10,10,10,10), 10), LocalDateTime.of(2000,10,10,10,10,9,990000000));
        assertEquals(opSubtract(ZonedDateTime.of(2000,10,10,10,10,10,0, ZoneOffset.UTC), 10),
                                ZonedDateTime.of(2000,10,10,10,10,0,0, ZoneOffset.UTC));

        assertEquals(opSubtract(s0, s2), new HashSet(Arrays.asList(1)));
        assertEquals(opSubtract(m1, s2), new HashMultiSet(Arrays.asList(1,1,2,3)));
        assertEquals(opSubtract(l1, s2), new HashMultiSet(Arrays.asList(1,1,2,3)));

        assertEquals(opSubtract(s0, m2), new HashMultiSet(Arrays.asList(1)));
        assertEquals(opSubtract(m1, m2), new HashMultiSet(Arrays.asList(1,1,2,3)));
        assertEquals(opSubtract(l1, m2), new HashMultiSet(Arrays.asList(1,1,2,3)));

        assertEquals(opSubtract(s0, l2), new HashMultiSet(Arrays.asList(1)));
        assertEquals(opSubtract(m1, l2), new HashMultiSet(Arrays.asList(1,1,2,3)));
        assertEquals(opSubtract(l1, l2), new HashMultiSet(Arrays.asList(1,1,2,3)));
    }

    @Test
    public void testOpConcat() {
        assertEquals(opConcat("abc", "def"), "abcdef");
    }

    @Test
    public void testOpBitShiftLeft() {
        assertEquals(opBitShiftLeft(1L, 2L), Long.valueOf(4L));
    }

    @Test
    public void testOpBitShiftRight() {
        assertEquals(opBitShiftRight(4L, 2L), Long.valueOf(1L));
    }

    @Test
    public void testOpBitAnd() {
        assertEquals(opBitAnd(5L, 3L), Long.valueOf(1L));
    }

    @Test
    public void testOpBitXor() {
        assertEquals(opBitXor(5L, 3L), Long.valueOf(6L));
    }

    @Test
    public void testOpBitOr() {
        assertEquals(opBitOr(5L, 3L), Long.valueOf(7L));
    }

    @Test
    public void testOpLike() {
        assertTrue(opLike("abc", "abc", null));
        assertTrue(opLike("abc", "_bc", null));
        assertTrue(opLike("abc", "%bc", null));
        assertTrue(opLike("abc", "a%bc", null));
        assertTrue(opLike("abc", "a%", null));
        assertTrue(opLike("a%bc", "a\\%%", "\\"));
        assertTrue(opLike("a_bc", "a\\_%", "\\"));
    }

}

