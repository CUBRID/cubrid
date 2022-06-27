
setenv CUBRID /opt/cubrid
setenv CUBRID_DATABASES $CUBRID/databases

if ( ${?LD_LIBRARY_PATH} ) then
	set LIB_PATH = `echo $LD_LIBRARY_PATH | grep -i cubrid`
	if ( $LIB_PATH == "" ) then
		setenv LD_LIBRARY_PATH $CUBRID/lib:$CUBRID/cci/lib:$LD_LIBRARY_PATH
	endif
else
	setenv LD_LIBRARY_PATH $CUBRID/lib:$CUBRID/cci/lib
endif

set BIN_PATH = `echo $path | grep -i cubrid`
if ( "$BIN_PATH" == "" ) then
	set path=($CUBRID/bin $path)
endif

set LIB=$CUBRID/lib

if ( -f /etc/redhat-release ) then
        set OS=`cat /etc/system-release-cpe | cut -d':' -f'3-3'`
else if ( -f /etc/os-release ) then
        set OS=`cat /etc/os-release | egrep "^ID=" | cut -d'=' -f2-2`
     endif
endif

switch ($OS)
  case "fedoraproject":
  case "centos":
  case "redhat":
  case "rocky":
    if ( ! -f /lib64/libncurses.so.5 && ! -f $LIB/libncurses.so.5 ) then
    	ln -s /lib64/libncurses.so.6 $LIB/libncurses.so.5
    	ln -s /lib64/libform.so.6 $LIB/libform.so.5
    	ln -s /lib64/libtinfo.so.6 $LIB/libtinfo.so.5
    endif
    breaksw
  case "ubuntu":
    if ( ! -f /lib/x86_64-linux-gnu/libncurses.so.5 && ! -f $LIB/libncurses.so.5 ) then
      ln -s /lib/x86_64-linux-gnu/libncurses.so.6 $LIB/libncurses.so.5
      ln -s /lib/x86_64-linux-gnu/libform.so.6 $LIB/libform.so.5
      ln -s /lib/x86_64-linux-gnu/libtinfo.so.6 $LIB/libtinfo.so.5
    endif
    breaksw
  case "debian":
    if ( ! -f /lib/x86_64-linux-gnu/libncurses.so.5 && ! -f $LIB/libncurses.so.5 ) then
            ln -s /lib/x86_64-linux-gnu/libncurses.so.6 $LIB/libncurses.so.5
            ln -s /lib/x86_64-linux-gnu/libtinfo.so.6 $LIB/libtinfo.so.5
            ln -s /usr/lib/x86_64-linux-gnu/libform.so.6 $LIB/libform.so.5
    endif
    breaksw
  default:
    echo "CUBRID requires the ncurses package. Make sure the ncurses package is installed"
    breaksw
endsw
