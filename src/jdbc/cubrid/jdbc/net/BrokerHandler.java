package cubrid.jdbc.net;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.sql.SQLException;
import java.net.UnknownHostException;
import java.security.KeyManagementException;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;

import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;

import cubrid.jdbc.jci.UConnection;
import cubrid.jdbc.jci.UErrorCode;
import cubrid.jdbc.jci.UJciException;
import cubrid.jdbc.jci.UTimedDataInputStream;

public class BrokerHandler {
    private static int TIMEOUT_UNIT = 1000;

    public static Socket connectBroker(String ip, int port, boolean useSSL, int timeout)
            throws IOException, UJciException {
        Socket toBroker = null;
        Socket toSSLBroker = null;
        UTimedDataInputStream in = null;
        DataOutputStream out = null;
        long begin = System.currentTimeMillis();

        try {
          toBroker = new Socket();
          InetSocketAddress brokerAddress = new InetSocketAddress(ip, port);
          if (timeout <= 0) {
            toBroker.connect(brokerAddress);
          }
          else {
            toBroker.connect(brokerAddress, timeout);
            timeout -= (System.currentTimeMillis() - begin);
            if (timeout <= 0) {
              toBroker.close();
              throw new UJciException(UErrorCode.ER_TIMEOUT);
            }
          }

          toBroker.setSoTimeout(TIMEOUT_UNIT);
          toBroker.setKeepAlive(true);
          in = new UTimedDataInputStream(toBroker.getInputStream(), ip, port, timeout);
          out = new DataOutputStream(toBroker.getOutputStream());

          if (useSSL == true) {
	          out.write(UConnection.driverInfossl);
          } else {
              out.write(UConnection.driverInfo);
          }

          out.flush();
          int code = in.readInt();
          if (code < 0) {
          // in here, all errors are sent by only a broker
          // the error greater than -10000 is sent by old broker
          if (code > -10000) {
              code -= 9000;
          }

          // There is an issue that cannot display an error text. (All cas error code)
          if (code == UErrorCode.CAS_ER_SSL_TYPE_NOT_ALLOWED) {
              throw new UJciException(UErrorCode.ER_DBMS, UErrorCode.CAS_ERROR_INDICATOR, code, "");
          } else {
              throw new UJciException(code);   
          }

	  } else if (code == 0) {
              if (useSSL == true) {
                  toSSLBroker = (Socket)createSSLSocket(toBroker, ip, port);
                  return (Socket)toSSLBroker;
              } else {
                  return toBroker;
              }
          }

          // if (code > 0) { only windows }
          toBroker.setSoLinger(true, 0);
          toBroker.close();

          toBroker = new Socket(); // need instantiation
          brokerAddress = new InetSocketAddress(ip, code);
          if (timeout <= 0) {
            toBroker.connect(brokerAddress);
          }
          else {
              timeout -= (System.currentTimeMillis() - begin);
              if (timeout <= 0) {
                  throw new UJciException(UErrorCode.ER_TIMEOUT);
              }
              toBroker.connect(brokerAddress, timeout);
          }

          toBroker.setKeepAlive(true);
          if (useSSL == true) {
              toSSLBroker = (Socket)createSSLSocket(toBroker, ip, code);
              return (Socket)toSSLBroker;
          } else {
              return toBroker;
          }
        } catch (SocketTimeoutException e) {
          if (toBroker != null) {
          toBroker.close();
        }
        throw new UJciException(UErrorCode.ER_TIMEOUT, e);
        } catch (IOException e) {
          if (toBroker != null) {
          toBroker.close();
        }
        throw new UJciException(UErrorCode.ER_CONNECTION, e);
        } finally {
        }
    }

    private static byte[] PING_INFO = { 'P', 'I', 'N', 'G', 0, 0, 0, 0, 0, 0 };
    public static void pingBroker(String ip, int port, int timeout)
            throws IOException {
        Socket toBroker = null;
        DataInputStream in = null;
        DataOutputStream out = null;
        long begin = System.currentTimeMillis();

        try {
          toBroker = new Socket();
          InetSocketAddress brokerAddress = new InetSocketAddress(ip, port);
          if (timeout <= 0) {
            toBroker.connect(brokerAddress);
          }
          else {
            toBroker.connect(brokerAddress, timeout);
            timeout -= (System.currentTimeMillis() - begin);
            if (timeout <= 0) {
              String msg = UErrorCode.codeToMessage(UErrorCode.ER_TIMEOUT);
              throw new SocketTimeoutException(msg);
            }
            toBroker.setSoTimeout(timeout);
          }

          in = new DataInputStream(toBroker.getInputStream());
          out = new DataOutputStream(toBroker.getOutputStream());
  
          out.write(PING_INFO);
          out.flush();
          in.readInt();
        } finally {
          if (in != null) in.close();
          if (out != null) out.close();
          if (toBroker != null) toBroker.close();
        }
    }

    private static int statusRequest(String ip, int port, byte[] data, int timeout)
            throws IOException, UJciException {
        Socket toBroker = null;
        DataInputStream in = null;
        DataOutputStream out = null;
        long begin = System.currentTimeMillis();

        try{
          toBroker = new Socket();
          InetSocketAddress brokerAddress = new InetSocketAddress(ip, port);
          if (timeout <= 0) {
            toBroker.connect(brokerAddress);
          }
          else {
            toBroker.connect(brokerAddress, timeout);
            timeout -= (System.currentTimeMillis() - begin);
            if (timeout <= 0) {
              throw new UJciException(UErrorCode.ER_TIMEOUT);
            }
            toBroker.setSoTimeout(timeout);
          }
    
          in = new DataInputStream(toBroker.getInputStream());
          out = new DataOutputStream(toBroker.getOutputStream());
    
          out.write(data);
          out.flush();
    
          int error = in.readInt();
          return error;
        } catch (SocketTimeoutException e) {
          throw new UJciException(UErrorCode.ER_TIMEOUT);
        } finally {
          if (in != null) in.close();
          if (out != null) out.close();
          if (toBroker != null) toBroker.close();
        }
    }

    private static void cancelRequest(String ip, int port, byte[] data, int timeout)
            throws IOException, UJciException {
        Socket toBroker = null;
        DataInputStream in = null;
        DataOutputStream out = null;
        long begin = System.currentTimeMillis();

        try {
          toBroker = new Socket();
          InetSocketAddress brokerAddress = new InetSocketAddress(ip, port);
          if (timeout <= 0) {
            toBroker.connect(brokerAddress);
          }
          else {
            toBroker.connect(brokerAddress, timeout);
            timeout -= (System.currentTimeMillis() - begin);
            if (timeout <= 0) {
              throw new UJciException(UErrorCode.ER_TIMEOUT);
            }
            toBroker.setSoTimeout(timeout);
          }

          in = new DataInputStream(toBroker.getInputStream());
          out = new DataOutputStream(toBroker.getOutputStream());

          out.write(data);
          out.flush();

          int error = in.readInt();
          if (error < 0) {
            throw new UJciException(UErrorCode.CAS_ER_QUERY_CANCEL);
          }
        } catch (SocketTimeoutException e) {
          throw new UJciException(UErrorCode.ER_TIMEOUT);
        } finally {
          if (in != null) in.close();
          if (out != null) out.close();
          if (toBroker != null) toBroker.close();
        }
    }

    private static byte[] CANCEL_INFO = { 'C', 'A', 'N', 'C', 'E', 'L' };
    private static byte[] STATUS_INFO = { 'S', 'T' };

    public static int statusBroker(String ip, int port, int process, byte[] session, int timeout)
        throws IOException, UJciException {

        int status;

        ByteArrayOutputStream bao = new ByteArrayOutputStream(10);
        DataOutputStream dao = new DataOutputStream(bao);
    
        dao.write(STATUS_INFO);
        dao.writeInt(process);
        for (int i = 0; i < 4; i++) dao.writeByte(session[i]);

        status = statusRequest(ip, port, bao.toByteArray(), timeout);

        return status;
    }

    public static void cancelBroker(String ip, int port, int process, int timeout)
            throws IOException, UJciException {
        ByteArrayOutputStream bao = new ByteArrayOutputStream(10);
        DataOutputStream dao = new DataOutputStream(bao);
        dao.write(CANCEL_INFO);
        dao.writeInt(process);

        cancelRequest(ip, port, bao.toByteArray(), timeout);
    }

    public static void cancelBrokerEx(String ip, int port, int process, int timeout)
            throws IOException, UJciException {
        ByteArrayOutputStream bao = new ByteArrayOutputStream(10);
        DataOutputStream dao = new DataOutputStream(bao);
        dao.write('X');
        dao.write('1');
        dao.write(UConnection.driverInfo[6]);
        dao.write(UConnection.driverInfo[7]);
        dao.write(UConnection.driverInfo[8]);
        dao.write(UConnection.driverInfo[9]);
        dao.writeInt(process);

        cancelRequest(ip, port, bao.toByteArray(), timeout);
    }

    private static SSLSocket createSSLSocket(Socket plainSocket, String ip, int port) throws UJciException {
        SSLSocket sslSocket = null;
        SSLContext ctx = null;
        SSLSocketFactory sslsocketfactory = null;

        X509TrustManager tm = new X509TrustManager() {
            public void checkClientTrusted(X509Certificate[] chain, String authType) throws CertificateException {
            }

            public void checkServerTrusted(X509Certificate[] xcs, String string) throws CertificateException {
            }

            public X509Certificate[] getAcceptedIssuers() {
                return new X509Certificate[0];
            }
        };

        try {
            ctx = SSLContext.getInstance("TLS");
        } catch (NoSuchAlgorithmException e) {
            e.printStackTrace();
            throw new UJciException(UErrorCode.ER_CONNECTION, e);
        }

        try {
            ctx.init(null, new TrustManager[] { tm }, new SecureRandom());
        } catch (KeyManagementException e) {
            e.printStackTrace();
            throw new UJciException(UErrorCode.ER_CONNECTION, e);
        }

        sslsocketfactory = ctx.getSocketFactory();
        try {
            sslSocket = (SSLSocket) sslsocketfactory.createSocket(plainSocket, ip, port, true);
            sslSocket.startHandshake();
        } catch (UnknownHostException e) {
            e.printStackTrace();
            throw new UJciException(UErrorCode.ER_CONNECTION, e);
        } catch (IOException e) {
            e.printStackTrace();
            throw new UJciException(UErrorCode.ER_CONNECTION, e);
        }

        return sslSocket;
    }
}
