export MYTAGSRCBASE=$PWD
export CSCOPE_DB="$MYTAGSRCBASE/cscope.out"
export MYTAG=$PWD/tags

function make_tag() {
  rm -f $MYTAG
  find $MYTAGSRCBASE -name '*.[ch]' -exec ctags -a $MYTAG {} \; -print;
  find $MYTAGSRCBASE -name '*.[ch]' > $MYTAGSRCBASE/cscope.files
  cd $MYTAGSRCBASE
  cscope -b
}

make_tag
