package nbench.report;
import java.util.LinkedList;
import java.io.FileReader;
import java.io.BufferedReader;
import java.io.IOException;
import java.util.regex.*;
import java.util.HashMap;

public class Reporter
{
  final static Pattern T_pat;
  final static Pattern m_pat;
  final static Pattern t_pat;
  static 
  {
    T_pat = 
	Pattern.compile("thread\\[(\\d+?)\\] interval\\[(\\d+?),(\\d+?)\\]");
    m_pat =
	Pattern.compile("mix\\[(\\d+?)\\]");
    String patt = 
	"(\\w+?)\\[(\\w+?)\\]," +
	"Time:(\\w+?)," +
	"Tot:(\\w+?)\\[succ:(\\w+?),fail:(\\w+?)\\]," +
	"Tps:([\\w\\.\\-]+?),Min:(\\w+?),Max:(\\w+?),"+
	"Avg:([\\w\\.\\-]+?),Med:([\\w\\.\\-]+?),R90:(\\w+?)";

    t_pat = Pattern.compile(patt);
  }
  
  /* ---------------------------------------------------------------------- */
  static class FilteredReporter
  /* ---------------------------------------------------------------------- */
  {
    long min_st; //minimum start time up to now
    long max_et; //maximum end time up to now
    NTotalStat total_stat;

    FilteredReporter()
    {
      min_st = Long.MAX_VALUE;
      max_et = Long.MIN_VALUE;
      total_stat = new NTotalStat();
    }
    
    void process_chunk(LinkedList<String> lines) throws Exception
    {
      int i;
      String l;
      String tid, st, et;
      String mid;

      /* check thread */
      l = lines.poll();
      Matcher T = T_pat.matcher(l);
      if(T.matches() == false)
	throw new Exception("unmatched line:" + l);
      tid = T.group(1);
      st = T.group(2);
      et = T.group(3);
      if(min_st > Long.valueOf(st).longValue())
	min_st = Long.valueOf(st).longValue();
      if(max_et < Long.valueOf(et).longValue())
	max_et = Long.valueOf(et).longValue();
      Long tid_long = Long.valueOf(tid);
      /* check mix */
      l = lines.poll();
      Matcher m = m_pat.matcher(l);
      if(m.matches() == false)
	throw new Exception("unmatched line:" + l);
      mid = m.group(1);
      /* check transaction */
      for(String line : lines)
      {
	Matcher t = t_pat.matcher(line);
	if(t.matches() == false)
	  throw new Exception("unmatched line:" + line);
	String type = t.group(1);
	String id = t.group(2);
	if(type.equals("mix") == false && type.equals("transaction") == false)
	  return;
	String time = t.group(3);
	String tot = t.group(4);
	String succ = t.group(5);
	String fail = t.group(6);
	String tps = t.group(7);
	String min = t.group(8);
	String max = t.group(9);
	String avg = t.group(10);
	String med = t.group(11);
	String r90 = t.group(12);

	if(Integer.valueOf(tot).intValue() <= 0)
	  return;
	NStat stat = new NStat(type + "[" + id + "]");
	stat.ntotal = Integer.valueOf(tot).intValue();
	stat.nsucc = Integer.valueOf(succ).intValue();
	stat.nfail = Integer.valueOf(fail).intValue();
	stat.time_sum = Integer.valueOf(time).intValue();
	stat.min = Integer.valueOf(min).intValue();
	stat.max = Integer.valueOf(max).intValue();
	

	int r90i = 0;
	int medi = 0;
	// user define time
	int ud_r90i = 0;
	int ud_medi = 0;
	// end user define time
	try 
	{
	  medi = Double.valueOf(med).intValue();
	  r90i = Double.valueOf(r90).intValue();
	  
	  total_stat.gather_stat(tid_long, stat, medi, r90i);

	}
	catch (Exception e)
	{
	  throw e;
	}
      }
    }
    void process(BufferedReader reader) throws Exception
    {
      LinkedList<String> chunk = new LinkedList<String>();
      while(true)
      {
	String line = reader.readLine();
	if(line == null || (line = line.trim()).length() == 0)
	  break;
	if(line.equals("--"))
	{
	  if(chunk.size() > 0)
	  {
	    process_chunk(chunk);
	    chunk = new LinkedList<String>();
	  }
	}
	else
	  chunk.add(line);
      }
    }
    void report()
    {
      StringBuffer sb = total_stat.report_all((int)(max_et - min_st));
      System.out.println(sb);
    }
  /* ---------------------------------------------------------------------- */
  }
  /* ---------------------------------------------------------------------- */

/**
 */
private static void print_usage()
{
  System.out.println(
"Reporter <logfiles> \n" +
"  <logfiles>\n\tnbench log files \n"
  );
}
/**
 */
public static void main (String args[])
{
  if(args.length < 1)
  {
    print_usage();
    return;
  }
  try 
  {
    FilteredReporter reporter = new FilteredReporter();
    for(int i = 0; i < args.length; i++)
    {
      System.out.print("Processing " + args[i] + " ... ");
      BufferedReader reader = new BufferedReader(new FileReader(args[i]));
      System.out.println("DONE");
      reporter.process(reader);
    }
    reporter.report();
  }
  catch (Exception e)
  {
    e.printStackTrace();
  }
}
} /* END Reporter */
