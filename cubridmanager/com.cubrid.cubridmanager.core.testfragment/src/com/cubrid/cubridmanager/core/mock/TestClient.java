package com.cubrid.cubridmanager.core.mock;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.Socket;

public class TestClient {

	/**
	 * TODO: how to write comments
	 * What and why the member function does what it does
	 * What a member function must be passed as parameters
	 * What a member function re turns
	 * Known bugs
	 * Any exceptions that a member function throws
	 * Visibility decisions
	 * How a member function changes the object
	 * Include a history of any code changes
	 * Examples of how to invoke th e member function if appropriate
	 * Applicable preconditions and postconditions
	 * Document all concurrency
	 * 
	 * @param args
	 */
	public static void main(String[] args) throws Exception {
		
		String basepath = MockTaskServer.class.getResource(".").getPath() + "scripts/";
		
		Socket sock = new Socket("localhost", 8002);
		BufferedReader br = null;
		BufferedWriter bw = null;
		
		try {
			
			bw = new BufferedWriter(new OutputStreamWriter(sock.getOutputStream()));
			String data = getFileText(basepath+"database.createdb.001.req.txt");
			bw.write(data);
			bw.flush();
			
			br = new BufferedReader(new InputStreamReader(sock.getInputStream()));
			StringBuilder sb = new StringBuilder();
			for (;;)
			{
				String line = br.readLine();
				if (line == null || "".equals(line.trim()))
					break;
				sb.append(line).append("\n");
			}
			
			System.out.println("<TestClient-res>\n"+sb.toString()+"\n</TestClient-res>");
			
		} catch (Exception e) {
			e.printStackTrace();
		}		
		finally {
			if (br != null) try { br.close(); } catch (Exception ignored) {}
			if (bw != null) try { bw.close(); } catch (Exception ignored) {}
			if (sock != null) try { sock.close(); } catch (Exception ignored) {}
		}

	}
			
	public static String getFileText(String filepath)
	{
		FileReader fr = null;
		BufferedReader br = null;
		try
		{
			StringBuilder sb = new StringBuilder();
			fr = new FileReader(new File(filepath));
			br = new BufferedReader(fr);
			for (;;)
			{
				String line = br.readLine();
				if (line == null)
					break;
				sb.append(line).append("\n");
			}
			return sb.toString();
		}
		catch (Exception ex)
		{
			return null;
		}
		finally
		{
			try { br.close(); } catch (Exception ignored) {}
			try { fr.close(); } catch (Exception ignored) {}
		}
	}		

}
