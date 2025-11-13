#include "page_buffer.h"
#include "thread_compat.hpp"


struct page_auto_unfix
{
  THREAD_ENTRY *thread_p = nullptr;

  void operator() (PAGE_PTR p) const noexcept
  {
    if (p != nullptr)
      {
	pgbuf_unfix_and_init_after_check (thread_p, p);
      }
  }
};

using Page = std::remove_pointer<PAGE_PTR>::type;
using auto_unfix_page_ptr = std::unique_ptr<Page, page_auto_unfix>;

// Factory that wraps pgbuf_fix with RAII
inline auto_unfix_page_ptr pgbuf_fix_auto_unfix (
	THREAD_ENTRY *thread_p,
	const VPID *vpid,
	PAGE_FETCH_MODE fetch_mode,
	PGBUF_LATCH_MODE request_mode,
	PGBUF_LATCH_CONDITION condition)
{
  PAGE_PTR p = pgbuf_fix (thread_p, vpid, fetch_mode, request_mode, condition);
  // If p == nullptr, UniquePagePtr is empty, but still carries the deleter with thread_p
  return auto_unfix_page_ptr (p, page_auto_unfix{thread_p});
}

