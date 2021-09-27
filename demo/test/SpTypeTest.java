package test;

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

public class SpTypeTest {
    public static int testRIntConst() {
        return 10;
    }

    public static void testOutInt(int i[]) {
        i[0] = i[0] + 10;
    }

    public static void testOutInt2(int i[]) {
        i[0] = i[0] * 1000000;
    }

    public static int testInt(int i) {
        return i + 1;
    }

    public static int testInt2(int i) {
        return i * 1000000;
    }

    public static int testIntArgs(int i, int j) {
        return i + j;
    }

    public static String testIntRString(int i, int j) {
        return i + j + "";
    }

    public static float testFloat(float f) {
        return f + 1;
    }

    public static double testDouble(double d) {
        return d + 1;
    }

    public static String testChar(String c) {
        return c + 1;
    }

    public static String testString(String s) {
        return "Result: " + s;
    }

    public static int testArray(int[] a) {
        return a.length;
    }

    public static java.sql.Date testDate(java.sql.Date d) {
        return d;
    }

    public static java.sql.Time testTime(java.sql.Time d) {
        return d;
    }

    public static java.sql.Timestamp testTimestamp(java.sql.Timestamp d) {
        return d;
    }

    // TODO :Monetary, OID, SET
}
