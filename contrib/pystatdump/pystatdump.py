#!/usr/bin/python

import sys, getopt, locale
from scipy.stats.stats import pearsonr
import numpy
from datetime import datetime
import matplotlib.pyplot as plt
import matplotlib.dates
import os.path
from sys import getsizeof
from scipy.spatial import distance
import platform

def replace_whitespaces(str):
    res = str
    res = res.replace (' ','')
    res = res.replace (',', '')
    res = res.replace (',', '')
    res = res.replace (';', '')
    res = res.replace('\t', '')
    res = res.replace('\n', '')
    res = res.replace('/', '')
    return res

def parse_sar(filepath, date_MMDDYY, has_multiple_inputs = 0):
    data = []
    dict = {}
    ref_dict = {}
    cnt_lines = 1
    ref_dict[TIME_COL] = ''
    ignored_empty_cnt = 0
    ignored_useless_cnt = 0
    ignored_error_cnt = 0

    print ('parse_sar : ' + filepath + ' date_MMDDYY:' + str (date_MMDDYY))

    if ('Linux' in platform.system()):
        locale.setlocale(locale.LC_ALL, 'en_US')
    else:
        locale.setlocale(locale.LC_ALL, 'English')

    ll = locale.getlocale(locale.LC_ALL)

    if (has_multiple_inputs):
        prefix = os.path.basename (filepath) + "."
    else:
        prefix = ''

    with open(filepath, 'r') as file_object:
        line = file_object.readline()
        cnt_lines = 1

        rowid = 0
        while line:
            if line.isspace():
                #
                ignored_empty_cnt = ignored_empty_cnt + 1
            elif 'Linux' in line:
                ignored_useless_cnt = ignored_useless_cnt + 1
            elif 'CPU'.lower() in line.lower():
                words = line.split()
                for col in words:
                    if (':' in col):
                        ref_dict[TIME_COL] = ''
                    elif '%' in col:
                        ref_dict[col] = ''
            else:
                if (ref_dict.keys ().__len__() < 2):
                    raise ("sar file : cannot find header")

                words = line.split()
                time_val = datetime.strptime (words[0], '%H:%M:%S')
                datetime_val = date_MMDDYY + ' ' + time_val.strftime ('%H:%M:%S')
                datetime_val = datetime.strptime (datetime_val, '%m/%d/%y %H:%M:%S')
                key = prefix + TIME_COL
                dict[key] = datetime_val
                #dict[CPU] = words[1]
                key = prefix + '%user'
                dict[key] = float (words[2])

                key = prefix + '%nice'
                dict[key] = float (words[3])

                key = prefix + '%system'
                dict[key] = float (words[4])

                key = prefix + '%iowait'
                dict[key] = float (words[5])

                key = prefix + '%steal'
                dict[key] = float (words[6])

                key = prefix + '%idle'
                dict[key] = float (words[7])

                key = prefix + '%working'
                dict[key] = 1 - dict[prefix + '%idle']
                if has_multiple_inputs:
                    dict[ROWID_COL] = rowid
                data.append (dict)
                dict = {}
                rowid = rowid + 1

            line = file_object.readline()
            cnt_lines = cnt_lines + 1

    print('Read lines:', cnt_lines)
    print('ignored_empty_cnt:', ignored_empty_cnt)
    print('ignored_useless_cnt:', ignored_useless_cnt)
    print('ignored_error_cnt:', ignored_error_cnt)
    print('Keys:', ref_dict.keys().__len__())

    return data

def parse_iostat(filepath, device_name, has_multiple_inputs = 0):
    data = []
    dict = {}
    ref_dict = {}
    cnt_lines = 1
    ref_dict[TIME_COL] = ''
    ignored_empty_cnt = 0
    ignored_useless_cnt = 0
    ignored_error_cnt = 0

    print('parse_iostat : ' + filepath + ' device_name:' + device_name)

    if ('Linux' in platform.system()):
        locale.setlocale(locale.LC_ALL, 'en_US')
    else:
        locale.setlocale(locale.LC_ALL, 'English')

    ll = locale.getlocale(locale.LC_ALL)

    if (has_multiple_inputs):
        prefix = os.path.basename (filepath) + "."
    else:
        prefix = ''

    with open(filepath, 'r') as file_object:
        line = file_object.readline()
        cnt_lines = 1

        rowid = 0
        while line:
            if line.isspace():
                ignored_empty_cnt = ignored_empty_cnt + 1
            elif "Device" in line:
                ignored_useless_cnt = ignored_useless_cnt + 1
            elif "Device" in line:
                ignored_useless_cnt = ignored_useless_cnt + 1
            elif '/' in line and ':' in line:
                line = line.strip()
                datetime_val = datetime.strptime (line, '%m/%d/%y %H:%M:%S')
                key = prefix + TIME_COL
                dict[key] = datetime_val
            elif device_name in line:
                if dict.keys ().__len__ () != 1:
                    raise ("Time row not found in iostat file")

                words = line.split()

                key = prefix + 'rkB/s'
                dict[key] = float (words[5])

                key = prefix + 'wkB/s'
                dict[key] = float (words[6])

                key = prefix + 'avgrq-sz'
                dict[key] = float(words[7])

                key = prefix + 'avgqu-sz'
                dict[key] = float(words[8])

                key = prefix + 'util'
                dict[key] = float(words[13])

                if has_multiple_inputs:
                    dict[ROWID_COL] = rowid
                data.append (dict)
                dict = {}
                rowid = rowid + 1
            else:
                ignored_useless_cnt = ignored_useless_cnt + 1

            line = file_object.readline()
            cnt_lines = cnt_lines + 1

    print('Read lines:', cnt_lines)
    print('ignored_empty_cnt:', ignored_empty_cnt)
    print('ignored_useless_cnt:', ignored_useless_cnt)
    print('ignored_error_cnt:', ignored_error_cnt)
    print('Keys:', ref_dict.keys().__len__())

    return data

def parse_statdump(filepath, has_multiple_inputs = 0, stats_filter_list = []):
    complex_stats_header_list = ['Num_data_page_fix_ext:',
                     'Num_data_page_promote_ext:',
                     'Num_data_page_promote_time_ext:',
                     'Num_data_page_unfix_ext:',
                     'Time_data_page_lock_acquire_time:',
                     'Time_data_page_hold_acquire_time:',
                     'Time_data_page_fix_acquire_time:',
                     'Time_data_page_unfix_time:',
                     'Num_mvcc_snapshot_ext:',
                     'Time_obj_lock_acquire_time:']
    data = []
    dict = {}
    ref_dict = {}
    cnt_lines = 1
    ref_dict[TIME_COL] = ''
    ignored_empty_cnt = 0
    ignored_useless_cnt = 0
    ignored_error_cnt = 0
    ignored_filter_cnt = 0
    row_cnt = 0
    GET_LOCAL = 'GET_LOCAL'
    local_timezone = ''

    print('parse_statdump : ' + filepath)

    if (has_multiple_inputs):
        prefix = os.path.basename (filepath) + "."
    else:
        prefix = ''

    if ('Linux' in platform.system()):
        locale.setlocale(locale.LC_ALL, 'en_US')
    else:
        locale.setlocale(locale.LC_ALL, 'English')

    ll = locale.getlocale(locale.LC_ALL)

    with open(filepath, 'r') as file_object:
        line = file_object.readline()
        cnt_lines = 1
        complex_stat_idx = -1
        stripped_line = line.strip ()

        while line:
            if line.isspace ():
                #
                ignored_empty_cnt = ignored_empty_cnt + 1
            elif 'The' in line:
                ignored_useless_cnt = ignored_useless_cnt + 1
            elif '***' in line:
                ignored_useless_cnt = ignored_useless_cnt + 1
            elif GET_LOCAL in line:
                words = line.split('=')
                local_timezone = words[1].strip()
            elif line in complex_stats_header_list or stripped_line in complex_stats_header_list:
                try:
                    complex_stat_idx = complex_stats_header_list.index (line)
                except:
                    complex_stat_idx = complex_stats_header_list.index(stripped_line)

                if complex_stat_idx == -1:
                    complex_stat_idx = complex_stats_header_list.index(stripped_line)

            elif '=' in line:
                words = line.split ('=')
                key_base = words[0].strip()

                if (complex_stat_idx != -1):
                    key = prefix + complex_stats_header_list[complex_stat_idx]+ '.' + key_base
                    key = key.replace (':','.')
                else:
                    key = prefix + key_base

                value = words[1].strip ()

                accept_stat = 1
                if len (stats_filter_list) > 0:
                    accept_stat = 0
                    if complex_stat_idx != -1:
                        curr_complex_stat = complex_stats_header_list[complex_stat_idx]
                        curr_complex_stat2 = curr_complex_stat.replace (':', '')
                        if curr_complex_stat in stats_filter_list:
                            accept_stat = 1
                        elif curr_complex_stat2 in stats_filter_list:
                            accept_stat = 1
                        elif complex_stats_header_list[complex_stat_idx] + '.' + key_base in stats_filter_list:
                            accept_stat = 1
                    elif key_base in stats_filter_list:
                        accept_stat = 1

                if accept_stat:
                    # only numeric values are added in dictionaries
                    try:
                        num_val = float (value)
                        dict[key] = num_val

                        if not key in ref_dict.keys ():
                            ref_dict[key] = num_val
                    except:
                        ignored_useless_cnt = ignored_useless_cnt + 1
                else:
                    ignored_filter_cnt = ignored_filter_cnt + 1

            else:
                line = line.strip()
                try:
                    #dt = parser.parse (line)
                    if local_timezone == '':
                        raise ('Local timezone not found in statdump file ;' + 'Missing line containing :' + GET_LOCAL)

                    dt = datetime.strptime (line, STATDUMP_TIME_FORMAT1 + local_timezone + STATDUMP_TIME_FORMAT2)
                    if (not dt is None):
                        if dict.__len__() > 0:
                            data.append (dict)
                            row_cnt  = row_cnt + 1
                            dict = {}

                        key = TIME_COL
                        dict[key] = dt
                        complex_stat_idx = -1
                except:
                    if ':' in line:
                        ignored_useless_cnt = ignored_useless_cnt + 1
                    else:
                        print (line)
                        ignored_error_cnt = ignored_error_cnt + 1


            line = file_object.readline()
            stripped_line = line.strip()
            cnt_lines = cnt_lines + 1

    if dict.__len__() > 0:
        data.append (dict)
        row_cnt = row_cnt + 1

    print ('Read lines:', cnt_lines)
    print ('ignored_empty_cnt:', ignored_empty_cnt)
    print ('ignored_useless_cnt:', ignored_useless_cnt)
    print ('ignored_error_cnt:', ignored_error_cnt)
    print ('ignored_filter_cnt:', ignored_filter_cnt)
    print ('Keys:', ref_dict.keys ().__len__ ())
    print ('Rows:' + str (len (data)) + ' ; ' + str (row_cnt))

    return data, ref_dict, local_timezone

def extend_data(data, ref_dict, add_row_id = 0):
    data_adj = []
    rowid = 0
    prev_row = {}

    print ('extend_data : Keys before extending :' + str (data[0].keys ().__len__()))
    for row in data:
        for k in ref_dict.keys():
            if not k in row.keys():
                row[k] = 0
            k_DELTA = k + '_DELTA'
            k_ACCUM = k + '_ACCUM'
            if k == TIME_COL:
                pass
            elif rowid == 0:
                row[k_DELTA] = 0
                row[k_ACCUM] = 0
            else:
                row[k_DELTA] = row[k] - prev_row[k]
                row[k_ACCUM] = row[k] + prev_row[k]

        if (add_row_id):
            row[ROWID_COL] = rowid

        data_adj.append(row)
        if row.keys().__len__() - add_row_id != 1 + 3 * (ref_dict.keys().__len__() - 1):
            raise ("row size should match")
        rowid = rowid + 1
        prev_row = row

    print ('Keys after extending:', prev_row.keys().__len__())
    print ('Rows:' + str (len (data_adj)) + ' ; ' + str (rowid))

    return data_adj

def plot_two_graphs(time_array, k1,k2,array1,array2, text_string):
    #fig = plt.figure ()
    axes = plt.axes ()
    myFmt = matplotlib.dates.DateFormatter('%H:%M:%S')
    axes.xaxis.set_major_formatter(myFmt)
    my_graph = plt.plot(time_array, array1, label=k1, color='b')
    plt.setp(my_graph, linewidth=1)

    my_graph = plt.plot(time_array, array2, label=k2, color='r')
    plt.setp(my_graph, linewidth=1)

    plt.legend(loc='lower left', title=text_string, bbox_to_anchor=(0.0, 1.01), ncol=1, borderaxespad=0, frameon=False,
           fontsize='xx-small')

    if (platform.system () in 'Windows'):
        filename = k1 + k2;
        filename = filename[:200] + '.png'
    else:
        filename = k1 + k2 + '.png'

    filename = replace_whitespaces (filename)

    if (graph_mode == 1 or graph_mode == 2):
        plt.savefig (filename, format = 'png', dpi=PLOT_DPI)

    plt.close()

#merges data2 into data1 by key 'merge_key'
#both data1 and data2 are arrays of dictionary
#if values of merge_key from data2 does not exist in data1, data is not merged
#if values of merge_key from data1 does not exist in data2, the first value of data2 is used to populate rows of merged_data
def merge_data (data1, data2, merge_key):
    merged_data = []
    rowid2=0
    rowid1=0
    while rowid1 < len (data1):
        row1 = data1[rowid1]
        key_value1 = row1[merge_key]

        if rowid2 < len (data2):
            row2 = data2[rowid2]
            key_value2 = row2[merge_key]

        if type(key_value2) != type (key_value1):
            raise ("Merge key do not have the same type")

        if key_value2 >= key_value1 or rowid2 >= len (data2):
            for col in row2.keys ():
                if (col != merge_key):
                    row1[col] = row2[col]

            merged_data.append (row1)
            rowid1 = rowid1 + 1
        else:
            rowid2 = rowid2 + 1

    if len (data1) == 0:
        merged_data = data2

    print ("Merged data rows cnt : " + str (len (merged_data)))
    print ("Merged data columns cnt  : " + str(len(merged_data[0].keys ())))

    return merged_data

def merge_data_with_autoscale (data1, data2, merge_key):
    merged_data = []
    rowid2=0
    rowid1=0

    len_1 = len (data1)
    len_2 = len (data2)
    scale_factor = len_1 / len_2

    while rowid1 < len_1:
        row1 = data1[rowid1]
        key_value1 = row1[merge_key]

        rowid2 = round (rowid1 / scale_factor)

        if rowid2 >= len_2:
            rowid2 = len_2 - 1
        if rowid2 < 0:
            rowid2 = 0

        row2 = data2[rowid2]
        key_value2 = row2[merge_key]

        if type(key_value2) != type (key_value1):
            raise ("Merge key do not have the same type")

        for col in row2.keys ():
            if (col != merge_key):
                row1[col] = row2[col]

        merged_data.append (row1)
        rowid1 = rowid1 + 1

    print ("Merged data rows cnt : " + str (len (merged_data)))
    print ("Merged data columns cnt  : " + str(len(merged_data[0].keys ())))

    return merged_data

def normalize (array, range):
    n_array = []
    min_v = min (array)
    max_v = max (array)
    if (max_v - min_v < 0.0001):
        n_array = array
    else:
        n_array = list (map (lambda x : range * (x - min_v) / (max_v - min_v), array))

    return n_array

def similarity_func (array1, array2):
    e_dst = 0
    n_array1 = []
    n_array2 = []

    try:
        n_array1 = normalize (array1, 100)
        n_array2 = normalize (array2, 100)
        min1 = min (array1)
        min2 = min(array2)
        max1 = max(array1)
        max2 = max(array2)
        r1 = [min1, max1]
        r2 = [min2, max2]
        range1 = max1 - min1
        range2 = max2 - min2

        e_dst = distance.euclidean(n_array1, n_array2)
        e_dst = e_dst / len (array1)

        if range2 > range1 and range1 > 0.001:
            scale_f = range2 / range1
        elif range1 > range2 and range2 > 0.001:
            scale_f = range1 / range2
        else:
            scale_f = 1

        adj_dist = scale_f * e_dst
    except:
        pass

    return e_dst, adj_dist, r1, r2

#finds a similarity between columns 'k1' and 'k2' in dataset 'data'
#ignores series of 'k2' having no variation
def find_similarity1(data, k1, k2):
    array1 = []
    array2 = []
    time_array = []

    print ('find_similarity1', k1,k2)
    row = data[0]
    if not k1 in row.keys():
        raise ("Key not in row")
    if not k2 in row.keys():
        raise ("Key not in row")

    for row in data:
        val1 = row[k1]
        val2 = row[k2]
        time_value = row[TIME_COL]
        #time_value = time_value.strftime('%H:%M:%S')
        #time_value = datetime.strptime (time_value, '%H:%M:%S')

        array1.append (val1)
        array2.append (val2)
        time_array.append (time_value)


    try:
        e_dst, adj_dist, range1, range2 = similarity_func(array1, array2)
    except:
        pass
    print (e_dst, adj_dist)
    if e_dst < similarity_1_threshold and adj_dist < similarity_2_threshold and range2[1] > range2[0]:
        text_string = 'Sim = ' + "{:.4f}".format (e_dst) + ' ; ' +  "{:.4f}".format (adj_dist)
        text_string = text_string + ' Range:' + "{:.2f}".format (range1[0]) + ' , ' +  "{:.2f}".format (range1[1])
        text_string = text_string + ' ;' + "{:.2f}".format(range2[0]) + ' , ' + "{:.2f}".format(range2[1])
        plot_two_graphs (time_array, k1, k2, array1, array2, text_string)

def find_similarity2_func (data, k1, k2):
    array1 = []
    array2 = []
    time_array = []

    print ('find_similarity2_func (' + k1 + ', ' + k2 + ')')

    for row in data:
        val1 = row[k1]
        val2 = row[k2]
        time_value = row[TIME_COL]
        #time_value = time_value.strftime('%H:%M:%S')
        #time_value = datetime.strptime (time_value, '%H:%M:%S')

        array1.append (val1)
        array2.append (val2)
        time_array.append (time_value)

    e_dst = 0
    n_array1 = []
    n_array2 = []

    e_dst, adj_dist, range1, range2 = similarity_func (array1, array2)

    print (e_dst, adj_dist)
    if e_dst > similarity_1_threshold or adj_dist > similarity_2_threshold :
        text_string = 'Sim = ' + "{:.4f}".format (e_dst) + " ; " + "{:.4f}".format (adj_dist)
        text_string = text_string + ' Range:' + "{:.2f}".format (range1[0]) + ' , ' +  "{:.2f}".format (range1[1])
        text_string = text_string + ' ;' + "{:.2f}".format(range2[0]) + ' , ' + "{:.2f}".format(range2[1])
        plot_two_graphs (time_array, k1, k2, array1, array2, text_string)

#finds a worst similarity of same-keys between subsets 'prefix1' and 'prefix2' in dataset 'data'
def find_similarity2(data, prefix1, prefix2):
    keys_list1 = []
    keys_list2 = []

    print ('find_similarity2', prefix1, prefix2)
    row = data[0]

    for key in row.keys():
        if prefix1 in key:
            keys_list1.append (key)
        if prefix2 in key:
            keys_list2.append (key)

    print ('Keys with prefix ' + prefix1 + ' ' + str (keys_list1.__len__()))
    print ('Keys with prefix ' + prefix2 + ' ' + str (keys_list2.__len__()))

    for key1 in keys_list1:
        base_key1 = key1.replace (prefix1,'')
        found = 0
        for key2 in keys_list2:
            base_key2 = key2.replace(prefix2, '')
            if (base_key1 == base_key2):
                find_similarity2_func (data, key1, key2)
                found = 1
        if found == 0:
            print('Key ' + prefix1 + ' + ' + base_key1 + ' not found')

def collect_data (data, prefix, suffix):
    array_data = []
    for row in data:
        for key in row.keys():
            if key == prefix + suffix:
                array_data.append (row[key])

    return array_data

def collect_data_from_table (table, colid):
    array_data = []

    size_of_table = numpy.shape (table)
    row_cnt = size_of_table[0]
    col_cnt = size_of_table[1]

    for rowid in range (0, row_cnt):
        array_data.append (table [rowid][colid])

    return array_data

def convert_to_table (data):
    row = data[0]
    col_positions = {}
    keys = row.keys ()
    table = []

    row_cnt = len (data)
    col_cnt = keys.__len__ ()

    print('  converting to table ; rows: ' + str (row_cnt) + '; col_cnt:' + str (col_cnt))

    #table = numpy.empty (row_cnt, col_cnt, dtype = 'float')

    col_id = 0
    for key in keys:
        col_positions[key] = col_id
        col_id = col_id + 1

    row_id = 0
    for row in data:
        table_row = []
        for key in keys:
            col_id = col_positions[key]
            value = row[key]
            table_row.append (value)

        table.append (table_row)

        row_id = row_id + 1

    print('  converted to table ; sizeof table: ' + str (getsizeof (table)))

    return table, col_positions


def stack_graphs (data, prefix_list, suffix_list):
    row = data[0]
    filtered_suffix_list = []

    print ('stack_graphs')

    data_table, col_positions = convert_to_table (data)

    prefix_list_cnt = len (prefix_list)

    for key in row.keys():
        if prefix_list[0] in key:
            suffix_key = key
            suffix_key = suffix_key.replace(prefix_list[0], '')

            found = 0
            if suffix_list.__len__() > 0:
                if suffix_key in suffix_list:
                    found = 1
            else:
                found = 1

            if found:
                filtered_suffix_list.append (suffix_key)

    print ('Found keys:' + str (filtered_suffix_list.__len__()))

    color_set = ['b', 'r', 'y', 'g', 'k', 'pink']

    for suffix in filtered_suffix_list:
        # start a new graph
        #fig, ax1 = plt.subplots()

        plt.figure ()

        print('    Drawing ' + suffix)

        graph_id = 0
        for prefix in prefix_list:
            print('          Collecting data from:  ' + prefix + suffix)

            try:
                colid = col_positions[prefix + suffix]
            except KeyError:
                print ('Data not found :' + prefix + suffix )
                graph_id = graph_id + 1
                continue

            array1 = collect_data_from_table (data_table, colid)

            my_graph = plt.plot(array1, label=prefix, color= color_set[graph_id])
            plt.setp(my_graph, linewidth=1)
            #plt.set_ylabel(prefix, color=color_set[graph_id])
            graph_id = graph_id + 1

        leg = plt.legend(loc='lower left', title = suffix, bbox_to_anchor= (0.0, 1.01 - 0.04 * (prefix_list_cnt - 3) ), ncol=1, borderaxespad=0, frameon=False, fontsize = 'xx-small')
        plt.setp(leg.get_title(), fontsize='xx-small')

        #plt.text(0, 0, suffix, fontsize=8)
        #plt.tight_layout(rect=[0, 0.2, 1, 1])

        filename = suffix
        filename = replace_whitespaces(filename)

        filename = filename.replace ('.', '')

        if (platform.system() in 'Windows'):
            filename = filename[:200]


        filename = filename + '.png'

        plt.savefig (filename, format='png', dpi=PLOT_DPI)

        plt.close ()

def parse_stats_filter_file(stats_filter_filename):
    stats_filter_list = []
    with open(stats_filter_filename, 'r') as file_object:
        line = file_object.readline()
        while line:
            if not '#' in line:
                line = line.strip()
                stats_filter_list.append (line)
            line = file_object.readline()

    print ('parse_stats_filter_file:' + str (len(stats_filter_list)) + ' filters')
    return stats_filter_list

def help():
    print('pystatdump.py -i <inputfile> ; statdump file output by CUBRID statdump utility (for better display in graphs rename it with version name)')
    print('              -m <mode> | --mode=<mode>; possible values sim1, sim2, stack')
    print('              --sar-file=<SAR file path>')
    print('              --iostat-file=<IOSTAT file path>')
    print('              --graph-mode=<output of graphs>')
    print('              --sim1-key=<string>')
    print('              -p; --prefix=<string>')
    print('              --sim1-th=<similarity_threshold 1> -sim2-th=<similarity_threshold 2>')
    print('              --stats-filter-file=<file containing desired stats (by default all stats are read)')
    print ('')
    print('  mode : sim1 : finds best similarity amount other keys for key provided by sim1-key argument')
    print('  mode : sim2 : for two instance of test (provided using prefix argument ), provides key which do not match (exceeds the similarity threshold)')
    print('  mode : stack : build stacked graphs (output in files) of each statistic from several versions (each specified by --prefix)')
    print('  sim1-key : name of statistic to be used for finding best similarity within range ')
    print('  sim1-th : first value of threshold (for sim1 mode is minimum the first column)')
    print('  sim2-th : second value of threshold (for sim1 mode is maximum of second column)')
    print('  graph-mode : 0 : display; 1 : file : 2 : both file and display')
    print('  auto-scale : 0, 1 ; enable auto-scale of X axis to match two series')
    print('')
    print('  Multiple -i -iostat-file -sar-file -prefix arguments may be provided, composing a list of arguments of the same kind.')
    print('  The order of --prefix argument matters in stack mode for the order of drawing')

def main(argv):
   mode = 'sim1'
   sar_list = []
   sar_filepath = ''
   inputfile_list = []
   iostat_list = []
   iostat_filepath = ''
   iostat_device = 'sda'
   sim1_key = 'Time_ha_replication_delay_msec'
   similarity1_threshold = 1
   similarity_2_threshold = 1
   has_multiple_inputs = 0
   graph_mode = 2
   prefix_list = ['OLD_', 'CURRENT_']
   has_prefix_arg = 0
   suffix_list = []
   stats_filter_file = ''
   stats_filter_list = []
   PLOT_DPI = 800
   merge_mode = 'noscale'
   statdump_local_timezone = ''

   try:
      opts, args = getopt.getopt(argv,"hi:m:p",["ifile=","mode=","sim1-key=", "sim1-th=", "sim2-th=", "sar-file=", "iostat-file=", "iostat-device=", "graph-mode=", "prefix=", "stats-filter-file=", "auto-scale="])
   except getopt.GetoptError:
      print ('Invalid arguments')
      help()
      sys.exit(2)
   for opt, arg in opts:
      if opt == '-h':
         help ()
         sys.exit()
      elif opt in ("-i", "--ifile"):
         inputfile_list.append (arg)
      elif opt in ("-m", "--mode"):
         mode = arg
      elif opt in ("--sim1-key"):
         sim1_key = arg
      elif opt in ["--sim1-th"]:
         similarity1_threshold = float (arg)
      elif opt in ["--sim2-th"]:
          similarity_2_threshold = float (arg)
      elif opt in ["--sar-file"]:
         sar_list.append (arg)
      elif opt in ["--iostat-file"]:
         iostat_list.append (arg)
      elif opt in ["--iostat-device"]:
          iostat_device = arg
      elif opt in ["--graph-mode"]:
          graph_mode = arg
      elif opt in ["--prefix"]:
          if has_prefix_arg == 0:
              prefix_list = []
              has_prefix_arg = 1
          prefix_list.append (arg)
      elif opt in ["--stats-filter-file"]:
          stats_filter_file = arg
      elif opt in ["--auto-scale"]:
          if arg == 1:
              merge_mode = 'autoscale'


   if len (stats_filter_file) > 0:
       stats_filter_list = parse_stats_filter_file(stats_filter_file)


   for inputfile in inputfile_list:
        print ('Input file :', inputfile)
   print ('Mode: ', mode)
   print('sim1_key :', sim1_key)
   for sar_filepath in sar_list:
        print('sar_filepath :', sar_filepath)
   for iostat_filepath in iostat_list:
        print('iostat_filepath :', iostat_filepath)
   print('similarity_1_threshold :', similarity_1_threshold)
   print('similarity_2_threshold :', similarity_2_threshold)
   print ('stats_filter_file :', stats_filter_file)

   if (inputfile_list.__len__() > 1 or sar_list.__len__() > 1 or iostat_list.__len__() > 1) :
       has_multiple_inputs = 1

   if mode in ['sim1', 'sim2']:
       merge_mode = 'autoscale'

   is_first_input_file = 1
   date_MMDDYY=''
   data = []
   for inputfile in inputfile_list:
      stat_data, columns, statdump_local_timezone = parse_statdump (inputfile, has_multiple_inputs, stats_filter_list)

      first_row = stat_data[0]
      first_time_value = first_row[TIME_COL]
      date_MMDDYY = first_time_value.strftime ('%m/%d/%y')

      ext_stat_data = extend_data (stat_data, columns, has_multiple_inputs)

      if has_multiple_inputs == 0 or is_first_input_file == 1:
          data = ext_stat_data
      else:
          if merge_mode == 'noscale':
              data = merge_data(data, ext_stat_data, ROWID_COL)
          else:
              data = merge_data_with_autoscale (data, ext_stat_data, ROWID_COL)


      is_first_input_file = 0


   sar_data = []

   for sar_filepath in sar_list:
        sar_data = parse_sar (sar_filepath, date_MMDDYY, has_multiple_inputs)
        if has_multiple_inputs:
            if merge_mode == 'noscale':
                data = merge_data(data, sar_data, ROWID_COL)
            else:
                data = merge_data_with_autoscale(data, sar_data, ROWID_COL)
        else:
            data = merge_data(data, sar_data, TIME_COL)

   for iostat_filepath in iostat_list:
       iostat_data = parse_iostat (iostat_filepath, iostat_device, has_multiple_inputs)
       if has_multiple_inputs:
           if merge_mode == 'noscale':
                data = merge_data(data, iostat_data, ROWID_COL)
           else:
               data = merge_data_with_autoscale(data, iostat_data, ROWID_COL)
       else:
           data = merge_data(data, iostat_data, TIME_COL)

   print ('')
   print ('size of data : ' + str (getsizeof (data)))
   print ('')

   if mode.lower() in 'sim1'.lower():
       columns = data[0]
       for k in columns.keys ():
           if k != 'time' and not 'replication' in k:
               find_similarity1 (data, sim1_key, k)

   elif mode.lower() in 'sim2'.lower():
       find_similarity2 (data, prefix_list[0], prefix_list[1])

   elif mode.lower() in 'stack'.lower():
       stack_graphs (data, prefix_list, suffix_list)

   if (graph_mode == 0 or graph_mode == 2):
       plt.show()


TIME_COL = 'time'
ROWID_COL = 'rowid'
STATDUMP_TIME_FORMAT1 = '%a %B %d %H:%M:%S '
STATDUMP_TIME_FORMAT2 = ' %Y'
inputfile_list = []
sar_filepath = ''
iostat_filepath = ''
iostat_device = ''
sim1_key = ''
similarity_1_threshold = 1
similarity_2_threshold = 1
graph_mode = 2
prefix_list = ['OLD_', 'CURRENT_']
stats_filter_file = ''
stats_filter_list = []
PLOT_DPI=600
merge_mode = 'noscale'



if __name__ == "__main__":
   main(sys.argv[1:])



# --mode sim1 -i 10.2.0.8167-5c392ad_statdump.raw --sim1-th=1 --sar-file=10.2.0.8167-5c392ad_sar_cpu.raw --iostat-file=10.2.0.8167-5c392ad_iostat.raw
# --mode sim2 --sim1-th=1 --sim2-th=1  --graph-mode=1 -i 10.2.0.7864-d585e59_statdump.raw -i 10.2.0.8167-5c392ad_statdump.raw --prefix=10.2.0.7864-d585e59_statdump.raw --prefix=10.2.0.8167-5c392ad_statdump.raw

# --mode stack -i 10.2.0.8167-5c392ad_statdump.raw -i 10.2.0.8107-a05cfaa_statdump.raw -i 10.2.0.7864-d585e59_statdump.raw.raw --prefix=10.2.0.8167-5c392ad_statdump.raw --prefix=10.2.0.8107-a05cfaa_statdump.raw --prefix=10.2.0.7864-d585e59_statdump.raw


# --mode stack --stats-filter-file=stats_filter.txt -i 10.2.0.8130-e96652f_statdump.raw -i 10.2.0.8126-e7da59e_statdump.raw -i 10.2.0.8167-5c392ad_statdump.raw -i 10.2.0.7864-d585e59_statdump.raw -i 10.2.0.8107-a05cfaa_statdump.raw --prefix=10.2.0.7864-d585e59_statdump.raw --prefix=10.2.0.8107-a05cfaa_statdump.raw --prefix 10.2.0.8126-e7da59e_statdump.raw --prefix=10.2.0.8130-e96652f_statdump.raw --prefix=10.2.0.8167-5c392ad_statdump.raw

# --mode stack --stats-filter-file=stats_filter.txt -i 10.2.0.8167-5c392ad_ES_statdump.raw  -i 10.2.0.7864-d585e59_statdump.raw -i 10.2.0.8167-5c392ad_statdump.raw  --prefix=10.2.0.8167-5c392ad_statdump.raw --prefix=10.2.0.8167-5c392ad_ES_statdump.raw
