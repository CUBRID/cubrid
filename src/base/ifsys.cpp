/*
 *
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * ifsys.cpp
 */

#include "ifsys.hpp"

#include <vector>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <dirent.h>
#include <sys/stat.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubbase
{
  bool ifsys::file_exists (const std::string &path)
  {
    struct stat st;

    return ::stat (path.c_str (), &st) == 0;
  }

  std::vector<std::string> ifsys::list_entries (const std::string &path)
  {
    std::vector<std::string> out;
    struct dirent *e;
    DIR *dir;
    std::string name;

    if ((dir = opendir (path.c_str ())))
      {
	while ((e = readdir (dir)))
	  {
	    name = e->d_name;
	    if (name == "." || name == "..")
	      {
		continue;
	      }
	    out.push_back (name);
	  }
	closedir (dir);
      }
    sort (out.begin (), out.end ());

    return out;
  }

  std::vector<std::string> ifsys::list_dirs_with_prefix (const std::string &path, const std::string &prefix)
  {
    std::vector<std::string> out;
    struct dirent *e;
    DIR *dir;
    std::string name;

    if ((dir = opendir (path.c_str ())))
      {
	while ((e = readdir (dir)))
	  {
	    if (e->d_type != DT_DIR && e->d_type != DT_LNK)
	      {
		continue;
	      }
	    name = e->d_name;
	    if (name == "." || name == "..")
	      {
		continue;
	      }
	    if (!prefix.empty () && name.rfind (prefix, 0) != 0)
	      {
		continue;
	      }
	    out.push_back (name);
	  }
	closedir (dir);
      }
    sort (out.begin (), out.end ());

    return out;
  }

  int ifsys::listdir_count_prefix (const char *path, const char *prefix)
  {
    struct dirent *e;
    DIR *dir;
    int length;
    int count;

    dir = opendir (path);
    if (!dir)
      {
	return 0;
      }

    count = 0;
    length = prefix ? strlen (prefix) : 0;
    while ((e = readdir (dir)))
      {
	if (strcmp (e->d_name, ".") == 0 || strcmp (e->d_name, "..") == 0)
	  {
	    continue;
	  }
	if (prefix && strncmp (e->d_name, prefix, length) != 0)
	  {
	    continue;
	  }
	if (e->d_type == DT_DIR || e->d_type == DT_LNK)
	  {
	    count++;
	  }
      }
    closedir (dir);

    return count;
  }

  std::string ifsys::read_one_line (const std::string &path)
  {
    std::ifstream file (path);
    std::string s;

    if (file.good ())
      {
	getline (file, s);
      }
    return s;
  }

  unsigned long long ifsys::read_u64 (const std::string &path)
  {
    std::ifstream file (path);
    unsigned long long v;

    v = 0;
    if (file.good ())
      {
	file >> v;
      }
    return v;
  }

  int ifsys::write_text (const char *path, const char *text)
  {
    FILE *file;
    int success;

    file = fopen (path, "w");
    if (!file)
      {
	return -1;
      }
    success = (fprintf (file, "%s\n", text) >= 0) ? 0 : -1;
    fclose (file);
    return success;
  }

  int ifsys::write_int (const char *path, long long int v)
  {
    char buf[64];

    snprintf (buf, sizeof (buf), "%lld", v);
    return write_text (path, buf);
  }

  char *ifsys::cpumask_hex_for_single_cpu (int cpu)
  {
    unsigned int v;
    std::size_t cap;
    char tmp[16];
    char *buf;
    int words;
    int i;

    words = cpu / 32 + 1;
    cap = (size_t) words * 9 + 1;
    buf = (char *) malloc (cap);
    if (!buf)
      {
	return NULL;
      }
    buf[0] = 0;

    for (i = words - 1; i >= 0; i--)
      {
	v = (i == cpu / 32) ? (1u << (cpu % 32)) : 0u;
	snprintf (tmp, sizeof (tmp), "%08x", v);
	if (buf[0] != '\0')
	  {
	    strncat (buf, ",", cap - strlen (buf) - 1);
	  }
	strncat (buf, tmp, cap - strlen (buf) - 1);
      }

    return buf;
  }

  bool ifsys::name_blacklisted (const std::string &ifname)
  {
    return ifname == "lo" ||
	   ifname.rfind ("docker",0) == 0 ||
	   ifname.rfind ("veth",0) == 0 ||
	   ifname.rfind ("br-",0) == 0 ||
	   ifname.rfind ("virbr",0) == 0 ||
	   ifname.rfind ("tun",0) == 0 ||
	   ifname.rfind ("tap",0) == 0 ||
	   ifname.rfind ("wg",0) == 0 ||
	   ifname.rfind ("podman",0) == 0 ||
	   ifname.rfind ("cni",0) == 0 ||
	   ifname.rfind ("flannel",0) == 0 ||
	   ifname.rfind ("cilium",0) == 0;
  }

  bool ifsys::is_physical_iface (const std::string &ifname)
  {
    return file_exists ("/sys/class/net/" + ifname + "/device");
  }

  bool ifsys::is_up (const std::string &ifname)
  {
    std::string s;

    s = read_one_line ("/sys/class/net/" + ifname + "/operstate");
    return (s == "up");
  }

  int ifsys::rx_queue_count (const std::string &ifname)
  {
    return list_dirs_with_prefix ("/sys/class/net/" + ifname + "/queues", "rx-").size ();
  }

  unsigned long long ifsys::traffic_score (const std::string &ifname)
  {
    unsigned long long rx, tx;

    rx = read_u64 ("/sys/class/net/" + ifname + "/statistics/rx_packets");
    tx = read_u64 ("/sys/class/net/" + ifname + "/statistics/tx_packets");
    return rx + tx;
  }

  std::string ifsys::auto_select_primary_iface ()
  {
    std::vector<std::string> ifs = list_entries ("/sys/class/net");
    unsigned long long best_score = 0;
    unsigned long long score;
    std::string best;

    for (auto &ifname : ifs)
      {
	if (name_blacklisted (ifname))
	  {
	    continue;
	  }
	if (!is_physical_iface (ifname))
	  {
	    continue;
	  }
	if (!is_up (ifname))
	  {
	    continue;
	  }
	if (rx_queue_count (ifname) == 0)
	  {
	    continue;
	  }

	/* TODO: use multiple NIC */
	score = traffic_score (ifname);
	if (score > best_score)
	  {
	    best = ifname;
	    best_score = score;
	  }
      }

    if (best.empty())
      {
	for (auto &ifname : ifs)
	  {
	    if (name_blacklisted (ifname))
	      {
		continue;
	      }
	    if (!is_physical_iface (ifname))
	      {
		continue;
	      }
	    if (!is_up (ifname))
	      {
		continue;
	      }
	    best = ifname;
	    break;
	  }
      }

    return best;
  }

  void ifsys::qirq_push (struct qirq_vec *a, int q, int irq)
  {
    int ncap;
    struct qirq *nv;

    if (a->n == a->cap)
      {
	ncap = a->cap ? a->cap * 2 : 32;
	nv = (struct qirq *) realloc (a->v, ncap * sizeof (*nv));
	if (!nv)
	  {
	    return;
	  }
	a->v = nv;
	a->cap = ncap;
      }
    a->v[a->n].q = q;
    a->v[a->n].irq = irq;
    a->n++;
  }

  int ifsys::parse_queue_suffix (const char *s)
  {
    int length;
    int i, j;

    length = (int) strlen (s);
    i = length - 1;
    while (i >= 0 && isspace ((unsigned char) s[i]))
      {
	i--;
      }
    if (i < 0)
      {
	return -1;
      }
    j = i;
    while (j >= 0 && isdigit ((unsigned char) s[j]))
      {
	j--;
      }
    if (j == i)
      {
	return -1;
      }
    return atoi (s + j + 1);
  }

  int ifsys::find_irqs_for_iface (const char *ifname, struct qirq_vec *out)
  {
    FILE *file;
    struct qirq tmp;
    char line[4096];
    char *last, *save, *tok;
    char *p, *colon;
    int q, w;
    int irq;

    file = fopen ("/proc/interrupts", "r");
    if (!file)
      {
	return -1;
      }

    while (fgets (line, sizeof (line), file))
      {
	p = line;
	while (isspace ((unsigned char) *p))
	  {
	    p++;
	  }
	if (!isdigit ((unsigned char) *p))
	  {
	    continue;
	  }
	irq = atoi (p);
	colon = strchr (p, ':');
	if (!colon)
	  {
	    continue;
	  }

	if (!strstr (line, ifname))
	  {
	    continue;
	  }

	last = NULL;
	save = NULL;
	tok = strtok_r (line, " \t\n", &save);
	while (tok)
	  {
	    last = tok;
	    tok = strtok_r (NULL, " \t\n", &save);
	  }

	if (!last)
	  {
	    continue;
	  }
	q = parse_queue_suffix (last);
	if (q < 0)
	  {
	    continue;
	  }

	qirq_push (out, q, irq);
      }
    fclose (file);

    if (out->n > 1)
      {
	for (int i=0; i<out->n; i++)
	  {
	    for (int j=i+1; j<out->n; j++)
	      {
		if (out->v[j].q < out->v[i].q)
		  {
		    tmp = out->v[i];
		    out->v[i]=out->v[j];
		    out->v[j]=tmp;
		  }
	      }
	  }
	w = 0;
	for (int i = 0; i < out->n; i++)
	  {
	    if (w == 0 || out->v[i].q != out->v[w-1].q)
	      {
		out->v[w++] = out->v[i];
	      }
	  }
	out->n = w;
      }
    return 0;
  }

  int ifsys::set_irq_affinity_list (int irq, int cpu)
  {
    char path[256], buf[32];
    char *mask;
    int success;

    snprintf (path, sizeof (path), "/proc/irq/%d/smp_affinity_list", irq);
    snprintf (buf, sizeof (buf), "%d", cpu);
    if (!write_text (path, buf))
      {
	return 0;
      }

    mask = cpumask_hex_for_single_cpu (cpu);
    if (!mask)
      {
	return -1;
      }
    snprintf (path, sizeof (path), "/proc/irq/%d/smp_affinity", irq);
    success = write_text (path, mask);
    free (mask);
    return success;
  }

  bool ifsys::set_rps_for_queue (const char *ifname, int rxq, int cpu)
  {
    char p1[320], p2[320];
    char base[256];
    char *mask;

    snprintf (base, sizeof (base), "/sys/class/net/%s/queues/rx-%d", ifname, rxq);
    snprintf (p1, sizeof (p1), "%s/rps_cpus", base);
    snprintf (p2, sizeof (p2), "%s/rps_flow_cnt", base);

    mask = cpumask_hex_for_single_cpu (cpu);
    if (!mask)
      {
	return false;
      }

    if (write_text (p1, mask) != 0)
      {
	return false;
      }
    /* TODO: is it better to turn this off? */
    //if (write_text (p2, "4096") != 0)
    if (write_text (p2, "0") != 0)
      {
	return false;
      }
    free (mask);

    return true;
  }

  bool ifsys::set_xps_for_queue (const char *ifname, int txq, int cpu)
  {
    char path[256];
    char *mask;

    mask = cpumask_hex_for_single_cpu (cpu);
    snprintf (path, sizeof (path), "/sys/class/net/%s/queues/tx-%d/xps_cpus", ifname, txq);
    if (!mask)
      {
	return false;
      }

    if (write_text (path, mask) != 0)
      {
	return false;
      }
    free (mask);
    return true;
  }

  void ifsys::maybe_set_rps_sock_flow_entries (int ncores)
  {
    const char *path = "/proc/sys/net/core/rps_sock_flow_entries";

    if (access (path, W_OK) != 0)
      {
	return;
      }
    /* TODO: is it better to turn this off? */
    //write_int (path, (long long int) ncores * 4096);
    write_int (path, 0);
  }
}

