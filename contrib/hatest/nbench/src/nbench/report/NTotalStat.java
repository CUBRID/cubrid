package nbench.report;
import java.util.TreeMap;

/**
 single user assummed.
 */
class NTotalStat
{
  /* --------------------------------------------------------------------- */
  /* END INNER CLASS */
  /* --------------------------------------------------------------------- */
  class NTotalStatItem
  {
    String sig;
    int ntotal;
    int nsucc;
    int nfail;
    int time_sum;
    int min;
    int max;
    int nmed;
    int nmed_sum;
    int nr90;
    int nr90_sum;
    
    /**
     */
    NTotalStatItem(String sig)
    {
      this.sig = sig;
      this.min = Integer.MAX_VALUE;
      this.max = Integer.MIN_VALUE;
    }
    /**
     */
    void merge(NTotalStatItem target)
    {
      ntotal += target.ntotal;
      nsucc += target.nsucc;
      nfail += target.nfail;
      if(min > target.min)
	min = target.min;
      if(max < target.max)
	max = target.max;
      nmed += target.nmed;
      nmed_sum += target.nmed_sum;
      nr90 += target.nr90;
      nr90_sum += target.nr90_sum;
    }
    /**
     */
    void gather(NStat ns, int med, int r90)
    {
      if(ns.ntotal == 0)
	return;
      ntotal += ns.ntotal;
      nsucc += ns.nsucc;
      nfail += ns.nfail;
      time_sum += ns.time_sum;
      if(min > ns.min)
	min = ns.min;
      if(max < ns.max)
	max = ns.max;
      nmed++;
      nmed_sum += med;
      nr90++;
      nr90_sum += r90;
            
    }
    void gather(NStat ns)
    {
      if(ns.ntotal == 0)
	return;
      gather(ns, (int)ns.get_rank((ns.ntotal + 1)/2), 
	     (int)ns.get_rank((int)(ns.ntotal*0.9)));
    }
    
    /**
     */
    private void report_int(StringBuffer sb, int time_sum)
    {
      sb.append ( 
	String.format(
	"%s Tot:%d[succ:%d,fail:%d],Min:%d,Max:%d,Med:%.2f,R90:%.2f," +
	"Time:%d,Avg:%.2f,Tps:%.2f\n"
        ,
        sig, ntotal, nsucc, nfail, 
	min, max, nmed_sum/(double)nmed, 
	nr90_sum/(double)nr90,
	time_sum, time_sum/(double)ntotal, (ntotal*1000)/(double)time_sum)
      );
    }
    /**
     */
    void report(StringBuffer sb)
    {
      report_int(sb, this.time_sum);
    }
    /**
     */
    void report(StringBuffer sb, int time_sum)
    {
      report_int(sb, time_sum);
    }
  }
  /* --------------------------------------------------------------------- */
  /* END INNER CLASS */
  /* --------------------------------------------------------------------- */
  /**
   */
  private TreeMap<Long, TreeMap<String, NTotalStatItem>> total_stats;
  /**
   */
  NTotalStat()
  {
    this.total_stats = new TreeMap<Long, TreeMap<String, NTotalStatItem>>();
  }
  /**
   */
  void gather_stat(Long tid, NStat ns)
  {
    TreeMap<String, NTotalStatItem> stat_map = total_stats.get(tid);
    if(stat_map == null)
    {
      stat_map = new TreeMap<String, NTotalStatItem>();
      total_stats.put(tid, stat_map);
    }
    NTotalStatItem tsi = stat_map.get(ns.sig);
    if(tsi == null)
    {
      tsi = new NTotalStatItem(ns.sig);
      stat_map.put(ns.sig, tsi);
    }
    tsi.gather(ns);
  }
  void gather_stat(Long tid, NStat ns, int med, int r90)
  {
    TreeMap<String, NTotalStatItem> stat_map = total_stats.get(tid);
    if(stat_map == null)
    {
//System.out.println(tid + " map");
      stat_map = new TreeMap<String, NTotalStatItem>();
      total_stats.put(tid, stat_map);
    }
    NTotalStatItem tsi = stat_map.get(ns.sig);
    if(tsi == null)
    {
//System.out.println(ns.sig + " total_stat");
      tsi = new NTotalStatItem(ns.sig);
      stat_map.put(ns.sig, tsi);
    }
    tsi.gather(ns, med, r90);
  }
 
  
  /**
   */
  StringBuffer report_all(int interval)
  {
    StringBuffer sb = new StringBuffer();
    sb.append( "\nTHROUGHPUT STATISTICS FOR INTERVAL[" + interval + " msec]\n");
    report_throughput(sb, interval);
    sb.append( "\nTRANSACTION STATISTICS FOR INTERVAL[" + interval + " msec]\n");
    report_transaction(sb, interval);
    return sb;
  }
  /**
   */
  void report_transaction(StringBuffer sb, int interval)
  {
    TreeMap<String, NTotalStatItem> total_tr_stat = 
	new TreeMap<String, NTotalStatItem>();

    //merge all mix and transaction statistics
    for(Long tid : total_stats.keySet())
    {
      TreeMap<String, NTotalStatItem> tr_stat = total_stats.get(tid);
      for(NTotalStatItem tsi : tr_stat.values())
      {
	if((tsi.sig.charAt(0) == 'm' && tsi.sig.charAt(1) == 'i' || 
	   tsi.sig.charAt(0) == 't' && tsi.sig.charAt(1) == 'r') == false)
	  continue;
	NTotalStatItem ttt = total_tr_stat.get(tsi.sig);
	if(ttt == null) //lazy
	{
	  ttt = new NTotalStatItem(tsi.sig);
	  total_tr_stat.put(ttt.sig, ttt);
	}
	ttt.merge(tsi);
      }
    }
    // report
    for(NTotalStatItem ttt : total_tr_stat.values())
      ttt.report(sb, interval);
  }
  /**
   */
  void report_throughput(StringBuffer sb, int interval)
  {
    int tr_succ_count = 0; /* 4.5.1 */
    int tr_fail_count = 0; /* 4.5.2 */
    int mix_succ_count = 0; /* 4.5.3 */
    int mix_fail_count = 0; /* 4.5.4 */
    for(Long tid : total_stats.keySet())
    {
      TreeMap<String, NTotalStatItem> tr_stat = total_stats.get(tid);
      for(NTotalStatItem tsi : tr_stat.values())
      {
	if(tsi.sig.charAt(0) == 'm' && tsi.sig.charAt(1) == 'i')
	{
	  mix_succ_count += tsi.nsucc;
	  mix_fail_count += tsi.nfail;
	}
	else if (tsi.sig.charAt(0) == 't' && tsi.sig.charAt(1) == 'r')
	{
	  tr_succ_count += tsi.nsucc;
	  tr_fail_count += tsi.nfail;
	}
      }
    }
    sb.append(String.format("Service requests processed:%d, average %.2f per second\n",
	tr_succ_count, (float)(tr_succ_count*1000.0/(double)interval)));
    sb.append(String.format("Service requests failed:%d, average %.2f per second\n",
	tr_fail_count, (float)(tr_fail_count*1000.0/(double)interval)));
    sb.append(String.format("PV succ: %d, average %.2f per second\n", 
	mix_succ_count, (float)(mix_succ_count*1000.0/(double)interval)));
  }
}
