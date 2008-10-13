
INCLUDES=-I$(top_srcdir)/src $(EXTRA_INCLUDES) 

bin_PROGRAMS = sample client server server_compliance_test memtest hello_client hello_server

sample_SOURCES = sample.c
sample_LDADD = -L@prefix@/lib ../src/libxmlrpc-epi.la

client_SOURCES = client.c
client_LDADD = -L@prefix@/lib ../src/libxmlrpc-epi.la

server_SOURCES = server.c
server_LDADD = -L@prefix@/lib ../src/libxmlrpc-epi.la

hello_client_SOURCES = hello_client.c
hello_client_LDADD = -L@prefix@/lib ../src/libxmlrpc-epi.la

hello_server_SOURCES = hello_server.c
hello_server_LDADD = -L@prefix@/lib ../src/libxmlrpc-epi.la

server_compliance_test_SOURCES = server_compliance_test.c
server_compliance_test_LDADD = -L@prefix@/lib ../src/libxmlrpc-epi.la

memtest_SOURCES = memtest.c
memtest_LDADD = -L@prefix@/lib ../src/libxmlrpc-epi.la

