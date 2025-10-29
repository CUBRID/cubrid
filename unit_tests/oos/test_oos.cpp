#include <iostream>
#include "test_oos.hpp"

int main (void)
{
  std::cout << "OOS unit tests placeholder." << std::endl;
  std::cout << "2 + 3 = " << add (2, 3) << std::endl;
  return 0;
}

int add (int a, int b)
{
  return a + b;
}

