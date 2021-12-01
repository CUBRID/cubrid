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

package test;

import java.util.Vector;

public class SpArgumentTest {

    /*
     * CREATE FUNCTION sp_plus_int(a INT, b INT) RETURN int as language java name 'SpArgumentTest.testPlusInt(int, int) return int';
     * SELECT sp_plus_int (1, 2);
     */
    public static int testPlusInt(int a, int b) {
        return a + b;
    }

    /*
     * CREATE FUNCTION sp_plus_int_out(a IN OUT INT, b IN OUT INT) RETURN int as language java name 'SpArgumentTest.testPlusIntOut(int[], int[]) return int';
     * SELECT sp_plus_intout (1, 2);
     */
    public static int testPlusIntOut(int a[], int b[]) {
        a[0] += 1;
        b[0] += 1;

        return a[0] + b[0];
    }

    /*
     * CREATE FUNCTION sp_plus_float(a FLOAT, b FLOAT) RETURN FLOAT as language java name 'SpArgumentTest.testPlusFloat(float, float) return float';
     * SELECT sp_plus_float (1.1, 2.1);
     */
    public static float testPlusFloat(float a, float b) {
        return a + b;
    }

    /*
     * CREATE FUNCTION sp_plus_float_out(a IN OUT FLOAT, b IN OUT FLOAT) RETURN FLOAT as language java name 'SpArgumentTest.testPlusFloatOut(float[], float[]) return float';
     * SELECT sp_plus_float_out (1.1, 2.1);
     */
    public static float testPlusFloatOut(float a[], float b[]) {
        a[0] += 1;
        b[0] += 1;

        return a[0] + b[0];
    }

    /*
     * CREATE PROCEDURE sp_setoid(x in out set, z object) AS LANGUAGE JAVA NAME 'SpArgumentTest.testSetOID(cubrid.sql.CUBRIDOID[][], cubrid.sql.CUBRIDOID)';
     */
    public static void testSetOID(cubrid.sql.CUBRIDOID[][] set, cubrid.sql.CUBRIDOID aoid) {
        String ret = "";
        Vector v = new Vector();

        cubrid.sql.CUBRIDOID[] set1 = set[0];
        try {
            if (set1 != null) {
                int len = set1.length;
                for (int i = 0; i < len; i++) v.add(set1[i]);
            }

            v.add(aoid);
            set[0] = (cubrid.sql.CUBRIDOID[]) v.toArray(new cubrid.sql.CUBRIDOID[] {});

        } catch (Exception e) {
            e.printStackTrace();
            System.err.println("SQLException:" + e.getMessage());
        }
    }
}
