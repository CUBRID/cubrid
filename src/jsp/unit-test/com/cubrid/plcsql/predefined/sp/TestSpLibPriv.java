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

package com.cubrid.plcsql.predefined.sp;

import static com.cubrid.plcsql.predefined.sp.SpLib.*;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;

import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.apache.commons.collections4.MultiSet;
import org.apache.commons.collections4.multiset.HashMultiSet;
import org.junit.Test;

public class TestSpLibPriv {
    List<Integer> l0 = Arrays.asList(1, 2, 3);
    List<Integer> l1 = Arrays.asList(1, 2, 3, 4);
    List<Integer> l2 = Arrays.asList(2, 3, 4);
    List<Integer> l3 = Arrays.asList(1, 2, 2, 3);
    List<Integer> l4 = Arrays.asList(1, 2, 2, 3, 3);
    Set s0 = new HashSet(l0);
    Set s1 = new HashSet(l1);
    Set s2 = new HashSet(l2);
    Set s3 = new HashSet(l3);

    @Test
    public void testCompareSets() {
        assertTrue(compareSets(s0, s3) == SetOrder.EQUAL);
        assertTrue(compareSets(s0, s1) == SetOrder.INCLUDED);
        assertTrue(compareSets(s1, s0) == SetOrder.INCLUDING);
        assertTrue(compareSets(s0, s2) == SetOrder.NONE);
    }

    MultiSet m0 = new HashMultiSet(l0);
    MultiSet m00 = new HashMultiSet(l0);
    MultiSet m1 = new HashMultiSet(l1);
    MultiSet m2 = new HashMultiSet(l2);
    MultiSet m3 = new HashMultiSet(l3);
    MultiSet m4 = new HashMultiSet(l4);

    @Test
    public void testCompareMultiSets() {
        assertTrue(compareMultiSets(m0, m0) == SetOrder.EQUAL);
        assertTrue(compareMultiSets(m0, m00) == SetOrder.EQUAL);
        assertTrue(compareMultiSets(m0, m3) == SetOrder.INCLUDED);
        assertTrue(compareMultiSets(m0, m1) == SetOrder.INCLUDED);
        assertTrue(compareMultiSets(m1, m0) == SetOrder.INCLUDING);
        assertTrue(compareMultiSets(m3, m0) == SetOrder.INCLUDING);
        assertTrue(compareMultiSets(m0, m2) == SetOrder.NONE);
    }

    @Test
    public void testCompareLists() {
        assertTrue(compareLists(l0, l0) == 0);
        assertTrue(compareLists(l0, l1) < 0);
        assertTrue(compareLists(l1, l0) > 0);
        assertTrue(compareLists(l0, l3) > 0);
    }

    @Test
    public void testSet2MultiSet() {
        assertEquals(m0, set2MultiSet(s0));
        assertEquals(m0, set2MultiSet(s3));
        assertNotEquals(m3, set2MultiSet(s3));
    }

    @Test
    public void testList2MultiSet() {
        assertEquals(m0, list2MultiSet(l0));
        assertEquals(m1, list2MultiSet(l1));
        assertEquals(m2, list2MultiSet(l2));
        assertEquals(m3, list2MultiSet(l3));
    }

    @Test
    public void testConcatLists() {
        assertEquals(Arrays.asList(1, 2, 3, 2, 3, 4), concatLists(l0, l2));
    }

    @Test
    public void testUnionSets() {
        assertEquals(s1, unionSets(s0, s2));
    }

    Set diff0 = new HashSet(Arrays.asList(1));
    Set diff2 = new HashSet(Arrays.asList(4));

    @Test
    public void testDiffSets() {
        assertEquals(diff0, diffSets(s0, s2));
        assertEquals(diff2, diffSets(s2, s0));
    }

    Set cap = new HashSet(Arrays.asList(2, 3));

    @Test
    public void testIntersectSets() {
        assertEquals(cap, intersectSets(s0, s2));
    }

    MultiSet m10 = new HashMultiSet(Arrays.asList(1, 2, 3, 1, 2, 3, 4));
    MultiSet m11 = new HashMultiSet(Arrays.asList(1, 2, 3, 2, 3, 4));

    @Test
    public void testUnionMultiSets() {
        assertEquals(m10, unionMultiSets(m0, m1));
        assertEquals(m11, unionMultiSets(m0, m2));
    }

    MultiSet m12 = new HashMultiSet();
    MultiSet m13 = new HashMultiSet(Arrays.asList(1));
    MultiSet m14 = new HashMultiSet(Arrays.asList(4));
    MultiSet m15 = new HashMultiSet(Arrays.asList(2));
    MultiSet m16 = new HashMultiSet(Arrays.asList(2, 3));

    @Test
    public void testDiffMultiSets() {
        assertEquals(m12, diffMultiSets(m0, m1));
        assertEquals(m13, diffMultiSets(m0, m2));
        assertEquals(m14, diffMultiSets(m2, m0));
        assertEquals(m15, diffMultiSets(m3, m0));
        assertEquals(m0, diffMultiSets(m4, m2));
        assertEquals(m16, diffMultiSets(m4, m0));
    }

    @Test
    public void testIntersectMultiSets() {
        assertEquals(m0, intersectMultiSets(m0, m1));
        assertEquals(m16, intersectMultiSets(m0, m2));
    }
}
