/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
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

package com.cubrid.jsp;

import java.io.File;
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.logging.FileHandler;
import java.util.logging.Level;
import java.util.logging.Logger;

public class Server
{
  private static String serverName;

  private static String spPath;

  private static String rootPath;

  private ServerSocket serverSocket;

  private static Logger logger = Logger.getLogger("com.cubrid.jsp");

  private static final String LOG_DIR = "log";

  public Server(String name, String path, String version, String rPath)
      throws IOException
  {
    serverName = name;
    spPath = path;
    rootPath = rPath;
    serverSocket = new ServerSocket(0);

    try
    {
      Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
    }
    catch (ClassNotFoundException e1)
    {
      e1.printStackTrace();
    }
    System.setSecurityManager(new SpSecurityManager());
    System.setProperty("cubrid.server.version", version);

    new Thread(new Runnable()
    {
      public void run()
      {
        Socket client = null;
        while (true)
        {
          try
          {
            client = serverSocket.accept();
            client.setTcpNoDelay(true);
            new ExecuteThread(client).start();
          }
          catch (IOException e)
          {
            log(e);
          }
        }
      }
    }).start();
  }

  private int getServerPort()
  {
    return serverSocket.getLocalPort();
  }

  public static String getServerName()
  {
    return serverName;
  }

  public static String getSpPath()
  {
    return spPath;
  }

  public static int start(String[] args)
  {
    try
    {
      Server server = new Server(args[0], args[1], args[2], args[3]);
      return server.getServerPort();
    }
    catch (Exception e)
    {
      e.printStackTrace();
    }

    return -1;
  }

  public static void main(String[] args)
  {
    Server.start(new String[] { "test" });
  }

  public static void log(Throwable ex)
  {
    FileHandler logHandler = null;

    try
    {
      logHandler = new FileHandler(rootPath + File.separatorChar + LOG_DIR
          + File.separatorChar + serverName + "_java.log", true);
      logger.addHandler(logHandler);
      logger.log(Level.SEVERE, "", ex);
    }
    catch (Throwable e)
    {
    }
    finally
    {
      if (logHandler != null)
      {
        try
        {
          logHandler.close();
          logger.removeHandler(logHandler);
        }
        catch (Throwable e)
        {
        }
      }
    }
  }
}
