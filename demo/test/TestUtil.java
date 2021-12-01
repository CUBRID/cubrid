package test;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.sql.SQLException;
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

    /* Test by reflection */
    private static void call(Object obj, String name, Object... args) {
        Class<?>[] types = new Class<?>[args.length];
        Object[] args2 = new Object[args.length];

        for (int i = 0; i < args.length; i++) {
            Object arg = args[i];

            if (arg instanceof Number) {
                if (arg instanceof Byte) {
                    types[i] = byte.class;
                } else if (arg instanceof Short) {
                    types[i] = short.class;
                } else if (arg instanceof Integer) {
                    types[i] = int.class;
                } else if (arg instanceof Long) {
                    types[i] = long.class;
                } else if (arg instanceof Float) {
                    types[i] = float.class;
                } else if (arg instanceof Double) {
                    types[i] = double.class;
                } else {
                    throw new IllegalArgumentException();
                }

                args2[i] = args[i];
            } else if (arg instanceof Boolean) {
                args2[i] = args[i];
                types[i] = boolean.class;
            } else if (arg instanceof Class) {
                args2[i] = null;
                types[i] = (Class<?>) arg;
            } else {
                args2[i] = args[i];
                types[i] = arg.getClass();
            }
        }

        try {
            Method method = obj.getClass().getMethod(name, types);
            method.invoke(obj, args2);
        } catch (SecurityException e) {
            throw new RuntimeException(e);
        } catch (NoSuchMethodException e) {
            throw new RuntimeException(e);
        } catch (IllegalArgumentException e) {
            throw new RuntimeException(e);
        } catch (IllegalAccessException e) {
            throw new RuntimeException(e);
        } catch (InvocationTargetException e) {
            Throwable th = e.getCause();
            if (((SQLException) th).getCause() instanceof UnsupportedOperationException) {
                System.out.println("OK");
            } else {
                throw new RuntimeException(th);
            }
        }
    }
}
