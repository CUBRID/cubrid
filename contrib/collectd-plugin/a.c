#include <stdio.h>

char *hostname_g;
int interval_g;
int main()
{
  printf("1\n");
  module_register();
  printf("2\n");
  return 0;
}

void plugin_register_read() { }
void plugin_register_init() { }
void plugin_dispatch_values() { }
void plugin_log() { }
