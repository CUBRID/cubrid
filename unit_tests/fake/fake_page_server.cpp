#include "fake_page_server.hpp"

cublog::replicator replicator_GL;
page_server ps_Gl;

namespace cublog
{
  void
  replicator::wait_past_target_lsa (const log_lsa &lsa)
  {
  }
}

cublog::replicator &
page_server::get_replicator ()
{
  return replicator_GL;
}
