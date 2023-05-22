# pystatdump
CUBRID statistics visualizer in Python

The tool is intended as graphic visualizer and investigation tool; is able to load multiple CUBRID statdump output files, SAR output files, IOSTAT output files.
All samples are gathered into one data container and mixed (using samples from SAR, IOSTAT, STATDUMP) analysis may be performed.
If at least two files of the same type are provided, all statistics are prefixed by filename of origin, otherwise no prefix is set to simplify operation.

Integral (_ACCUM suffix) and derived (_DELTA suffix) series are computed for each statistic series.
Statistics series may be filtered by providing a filters in a file (only the series included in the file are loaded from the statdump file).

There are three modes of operation:
 - stack : draws graphs for multiple version of the same statistics on the same picture; an image contains only one statistics.
 - sim1 : by providing a reference statistics (prefix.name), a comparison is performed on all other samples and if a similarity exceeding a threshold is found, a corresponding message is displayed and the comparative graph is shown.
        Usage hint: investigating the reasons for behavior of a statistic
 - sim2 : by providing two test instances (with the --prefix argument), by comparing the samples of the same name (suffix), if a strong similarity is not found, a correspoding message is displayed and the comparative graph is shown.
        Usage hint: find which values are "more different" between two runs.

Future improvements :
 - grouping of several hybrid series into the same graph (now is possible to plot several series but of the same type from different instances)


# Running

The tool requires Python3.6 and some packages:

sudo yum install python36u

sudo yum install python36u-pip

sudo yum install python36u-tkinter


sudo pip3.6 install scipy

sudo pip3.6 install matplotlib


You may need this to install Python3.6 in Centos7.x:

sudo yum install https://centos7.iuscommunity.org/ius-release.rpm

 
Use python3.6 pystatdump.py  <args>

To use without an X server, set this before running:

 export MPLBACKEND="agg"
