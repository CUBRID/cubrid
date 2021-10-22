package test;

import java.util.Arrays;

public class TestUtil {
    public static String assertTrue(boolean condition) {
        if (condition) {
            return "t ";
        } else {
            return "f ";
        }
    }

    public static String assertArrayEquals(Object[] expected, Object[] actual) {
        if (expected.length != actual.length) {
            return "f ";
        }

        if (!java.util.Arrays.equals(expected, actual)) {
            return "f value <" + Arrays.toString(expected) + "," + Arrays.toString(actual) + ">";
        }

        return "t ";
    }

    public static String assertArrayEquals(byte[] expected, byte[] actual) {
        if (expected.length != actual.length) {
            return "f length <" + expected.length + "," + actual.length + ">";
        }

        if (!java.util.Arrays.equals(expected, actual)) {
            return "f value <" + Arrays.toString(expected) + "," + Arrays.toString(actual) + ">";
        }

        return "t ";
    }

    public static String assertEquals(Object expected, Object actual) {
        if ((expected == null) && (actual == null)) {
            return "t ";
        }
        if (expected != null) {
            if (expected.getClass().isArray()) {
                return assertArrayEquals((Object[]) expected, (Object[]) actual);
            } else if (expected.equals(actual)) {
                return "t ";
            }
        }
        return "f <" + expected + "," + actual + ">";
    }

    public static String assertEquals(float expected, float actual, float eps) {
        if (Math.abs(expected - actual) <= eps) {
            return "t ";
        }

        return "f <" + expected + "," + actual + ">";
    }

    public static String assertEquals(double expected, double actual, double eps) {
        if (Math.abs(expected - actual) <= eps) {
            return "t ";
        }

        return "f <" + expected + "," + actual + ">";
    }
}
