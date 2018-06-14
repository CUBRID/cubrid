#ifndef _CUB_MASTER_MOCK_HPP
#define _CUB_MASTER_MOCK_HPP

#define LISTENING_PORT 6666
#define NUM_MOCK_SLAVES 4

namespace cub_master_mock
{
  int init ();
  int finish ();
  
  void stream_produce (unsigned int num_bytes);
}

#endif /* _CUB_MASTER_MOCK_HPP */
