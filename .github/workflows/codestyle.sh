f=$1  

ext=$(expr $f : ".*\(\..*\)")

case $ext in
.c|.h|.i)
  indent -l120 -lc120 ${f}
;;
.cpp|.hpp|.ipp)
  astyle --style=gnu --mode=c --indent-namespaces --indent=spaces=2 -xT8 -xt4 --add-brackets --max-code-length=120 --align-pointer=name --indent-classes --pad-header --pad-first-paren-out ${f}
;;
.java)
  java -jar google-java-format-1.7-all-deps.jar -a -r ${f}
;;
esac
