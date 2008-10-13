package com.cubrid.jsp;

import java.io.FileDescriptor;
import java.net.InetAddress;
import java.security.Permission;

public class SpSecurityManager extends SecurityManager {

    public void checkExit(int status) {
        throw new SecurityException();
    }

    public void checkAccept(String host, int port) {
    }

    public void checkAccess(Thread t) {
    }

    public void checkPermission(Permission perm, Object context) {
    }

    public void checkPermission(Permission perm) {
    }

    public void checkAccess(ThreadGroup g) {
    }

    public void checkAwtEventQueueAccess() {
    }

    public void checkConnect(String host, int port, Object context) {
    }

    public void checkConnect(String host, int port) {
    }

    public void checkCreateClassLoader() {
    }

    public void checkDelete(String file) {
    }

    public void checkExec(String cmd) {
    }

    public void checkLink(String lib) {
    }

    public void checkListen(int port) {
    }

    public void checkMemberAccess(Class clazz, int which) {
    }

    public void checkMulticast(InetAddress maddr, byte ttl) {
    }

    public void checkMulticast(InetAddress maddr) {
    }

    public void checkPackageAccess(String pkg) {
    }

    public void checkPackageDefinition(String pkg) {
    }

    public void checkPrintJobAccess() {
    }

    public void checkPropertiesAccess() {
    }

    public void checkPropertyAccess(String key) {
    }

    public void checkRead(FileDescriptor fd) {
    }

    public void checkRead(String file, Object context) {
    }

    public void checkRead(String file) {
    }

    public void checkSecurityAccess(String target) {
    }

    public void checkSetFactory() {
    }

    public void checkSystemClipboardAccess() {
    }

    public void checkWrite(FileDescriptor fd) {
    }

    public void checkWrite(String file) {
    }
}
