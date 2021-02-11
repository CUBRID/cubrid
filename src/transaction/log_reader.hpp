#ifndef LOG_READER_HPP
#define LOG_READER_HPP

#include "log_lsa.hpp"
#include "log_impl.h"

/* encapsulates reading of the log in different flavors
 */
class log_reader final
{
  public:
    log_reader ();
    log_reader (log_reader const& ) = delete;
    log_reader (log_reader&& ) = delete;

    log_reader &operator =(log_reader const& ) = delete;
    log_reader &operator =(log_reader&& ) = delete;

    const log_lsa& get_lsa() const;
    int set_lsa (const log_lsa& lsa);
    const log_hdrpage& get_page_header() const;
    //const char* get_cptr () const;

    //template <typename T>
    //void reinterpret_cptr (const T *& tptr) const;

    //void read_align ();  // equivalent to LOG_READ_ALIGN
    //void read_add_align (size_t size);
    //void advance_when_doesnt_fit (size_t size);

    //// and other read functions
    //template <typename T>
    //void read (const T& t);

  private:
    int fetch_page ();
    //void lsa_offset_align ();

  private:
    log_lsa m_lsa = NULL_LSA;
    log_page *m_page;
    char m_area_buffer[IO_MAX_PAGE_SIZE + DOUBLE_ALIGNMENT];
};

#endif // LOG_READER_HPP
