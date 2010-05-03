HDR = dict.h hashtable.h hb_tree.h pr_tree.h rb_tree.h sp_tree.h tr_tree.h\
	wb_tree.h
SRC = dict.c hashtable.c hb_tree.c pr_tree.c rb_tree.c sp_tree.c tr_tree.c\
	wb_tree.c
AOBJ = $(SRC:%.c=%.o)
POBJ = $(SRC:%.c=%.po)
SOBJ = $(SRC:%.c=%.So)

DEPDIR = .depend
DEP = $(SRC:%.c=$(DEPDIR)/%.dep)

LIB = dict
A_LIB = lib$(LIB).a
P_LIB = lib$(LIB)_p.a
S_LIB = lib$(LIB).so

CC ?= gcc
CFLAGS ?= -Wall -W -ansi -pedantic -DNDEBUG -g -O3

AR = ar
ARFLAGS = cru

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include/dict
MANDIR = $(PREFIX)/man
INSTALL ?= install
LIBVER = 2
SHLIB = $(S_LIB).$(LIBVER)
USER ?= 0
GROUP ?= 0

all : $(A_LIB) $(P_LIB) $(S_LIB)

$(A_LIB) : $(AOBJ)
	$(AR) $(ARFLAGS) $(A_LIB) $(AOBJ)

$(P_LIB) : $(POBJ)
	$(AR) $(ARFLAGS) $(P_LIB) $(POBJ)

$(S_LIB) : $(SOBJ)
	$(CC) -shared -o $(S_LIB) $(SOBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $(<) -o $(@)

%.po: %.c
	$(CC) $(CFLAGS) -pg -c $(<) -o $(@)

%.So : %.c
	$(CC) $(CFLAGS) -fPIC -DPIC -c $(<) -o $(@)

.PHONY : clean
clean :
	rm -f $(AOBJ) $(POBJ) $(SOBJ) $(A_LIB) $(P_LIB) $(S_LIB)
	-rm -rf $(DEPDIR)

install : $(A_LIB) $(P_LIB) $(S_LIB)
	[ -d $(INCDIR) ] || mkdir -m 755 $(INCDIR)
	$(INSTALL) -o $(USER) -g $(GROUP) -m 644 $(HDR) $(INCDIR)
	$(INSTALL) -s -o $(USER) -g $(GROUP) -m 644 $(A_LIB) $(LIBDIR)
	$(INSTALL) -s -o $(USER) -g $(GROUP) -m 644 $(P_LIB) $(LIBDIR)
	$(INSTALL) -s -o $(USER) -g $(GROUP) -m 755 $(S_LIB) $(LIBDIR)/$(SHLIB)
	$(SHELL) -ec 'cd $(LIBDIR) && ln -sf $(SHLIB) $(S_LIB)'

uninstall :
	-rm -rf $(INCDIR)
	-rm -f $(LIBDIR)/$(A_LIB)
	-rm -f $(LIBDIR)/$(S_LIB)

test : test.c $(A_LIB)
	$(CC) $(CFLAGS) -o $(@) $(<) $(A_LIB)
demo : demo.c $(A_LIB)
	$(CC) $(CFLAGS) -o $(@) $(<) $(A_LIB)

$(DEPDIR)/%.dep : %.c
	@$(SHELL) -c '[ -d $(DEPDIR) ] || mkdir $(DEPDIR)'
	@$(SHELL) -ec 'echo -n "Rebuilding dependencies for $< - "; \
		$(CC) -M $(CFLAGS) $< > $@; \
		sed "s/^$(<:%.c=%.o)/& $(<:%.c=%.po) $(<:%.c=%.So)/" $@ > $(DEPDIR)/$(<:%.c=%.dep2); \
		mv -f $(DEPDIR)/$(<:%.c=%.dep2) $@; \
		echo ok.'

-include $(DEP)
