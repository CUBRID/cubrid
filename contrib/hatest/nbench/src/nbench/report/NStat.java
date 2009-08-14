package nbench.report;

public class NStat
{
  String sig;
  public int ntotal;
  int nsucc;
  int nfail;
  int time_sum;
  int min;
  int max;

  boolean do_histo;
  int histogram_interval;
  int[] histogram;
  int out_histogram_count;
  public NStat(String sig, int num_histogram, int total_interval)
  { //TODO check if the assignment needed..
    this.sig = sig;
    ntotal = 0;
    nsucc = 0;
    nfail = 0;
    time_sum = 0;
    min = Integer.MAX_VALUE;
    max = Integer.MIN_VALUE;
    histogram = new int[num_histogram];
    histogram_interval = total_interval/num_histogram;
    for(int i = 0; i < histogram.length; i++)
    {
      histogram[i] = 0;
    }
    out_histogram_count = 0;
    do_histo = true;
  }
  public NStat(String sig)
  {
    this.sig = sig;
    ntotal = 0;
    nsucc = 0;
    nfail = 0;
    time_sum = 0;
    min = Integer.MAX_VALUE;
    max = Integer.MIN_VALUE;
    do_histo = false;
  }
  public void update(int interval, long timeout)
  {
    ntotal++;
    if(interval > timeout) 
      nfail++;
    else
      nsucc++;
    time_sum += interval;
    if(interval < min) min = interval;
    if(interval > max) max = interval;
    if(do_histo)
    {
      //update histogram
      int index = interval/histogram_interval;
      if(index >= histogram.length)
	out_histogram_count++;
      else
	histogram[index]++;
    }
  }

  public double get_rank(int target)
  {
    if(do_histo == false)
      return 0.0;
    double ret = -1.0;
    int ar = 0;
    for(int i = 0; i < histogram.length; i++)
    {
      if(ar + histogram[i] >= target)
      {
	ret = histogram_interval * i + 
		histogram_interval/(double)histogram[i] * (target - ar);
	break;
      }
      ar += histogram[i];
    }
    return ret;
  }
  
  public void report(StringBuffer sb, int index)
  {
      if(ntotal == 0)
	return; //nothing to report
      int avg = (int)((double)time_sum / (double)ntotal);
      int median = (int)get_rank((ntotal + 1)/2);
      int p90 = (int)get_rank((int)(ntotal*0.9));
      
      double tps = 1000.0/((double)time_sum / (double)ntotal);
      sb.append (
	String.format("%s,Time:%d,Tot:%d[succ:%d,fail:%d],Tps:%.2f,Min:%d,Max:%d,Avg:%d,Med:%d,R90:%d\n",
	  sig, time_sum, ntotal,nsucc,nfail, tps, min, max, avg, median, p90)
      );
  }
}
