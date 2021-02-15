#ifndef LOG_READER_HPP
#define LOG_READER_HPP

#include <type_traits>

#include "log_lsa.hpp"
#include "log_storage.hpp"
#include "log_impl.h"

/* encapsulates reading of the log in different flavors
 *
 * NOTE: not thread safe
 */
class log_reader final
{
  public:
    log_reader ();
    log_reader (log_reader const & ) = delete;
    log_reader (log_reader && ) = delete;

    log_reader &operator = (log_reader const & ) = delete;
    log_reader &operator = (log_reader && ) = delete;

    const log_lsa &get_lsa() const;
    int set_lsa_and_fetch_page (const log_lsa &lsa);
    const log_hdrpage &get_page_header() const;

    template <typename T>
    const typename std::remove_const< typename std::remove_reference<T>::type >::type *reinterpret_cptr () const;

    void add (size_t size);

    /* equivalent to LOG_READ_ALIGN
     */
    void align ();

    /* equivalent to LOG_READ_ADD_ALIGN
     */
    void add_align (size_t size);

    /* equivalent to LOG_READ_ADVANCE_WHEN_DOESNT_FIT
     */
    void advance_when_does_not_fit (size_t size);

    /* returns whether the supplied lengths is contained in the currently
     * loaded log page
     */
    bool is_within_current_page (size_t size) const;
    // function to copy directly
    // function to copy in external supplied buffer

    void copy_from_log (char *dest, size_t length);
    //int assign_ptr_or_alloc_and_copy_into_area_and_then_assign_ptr(char*& dest_ptr, raii_blob<char>& alloc_area, int length);

    //// and other read functions
    //template <typename T>
    //void read (const T& t);

    // TODO: somehow this function, add_align and advance_when_doesnt_fit
    // have the same core functionality and could be combined
    int skip (size_t size);

    bool equals (const log_lsa &other_log_lsa, const log_page &other_log_page) const;

  private:
    const char *get_cptr () const;

    int fetch_page ();
    //void lsa_offset_align ();

  private:
    log_lsa m_lsa = NULL_LSA;
    log_page *m_page;
    char m_area_buffer[IO_MAX_PAGE_SIZE + DOUBLE_ALIGNMENT];
};

/* implementation
 */

template <typename T>
const typename std::remove_const< typename std::remove_reference<T>::type >::type *log_reader::reinterpret_cptr () const
{
  using rem_ref_t = typename std::remove_reference<T>::type;
  using rem_const_t = typename std::remove_const< rem_ref_t >::type;
  const rem_const_t *p = reinterpret_cast<const rem_const_t *> (get_cptr());
  return p;
}

#endif // LOG_READER_HPP
