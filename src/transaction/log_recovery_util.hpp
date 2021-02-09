#ifndef LOG_RECOVERY_UTIL_HPP
#define LOG_RECOVERY_UTIL_HPP

/* helper class that takes care of a malloc'ed sequence of bytes
 */
template <typename T>
class raii_blob
{
  public:
    raii_blob()
      : blob (nullptr)
    {
    }

    raii_blob (const raii_blob &) = delete;
    raii_blob (raii_blob &&) = delete;

    ~raii_blob()
    {
      free();
    }

    raii_blob &operator= (const raii_blob &) = delete;
    raii_blob &operator= (raii_blob &&) = delete;

    void malloc (size_t size)
    {
      free();
      blob = static_cast<T *> (::malloc (size));
    }

    operator T *()
    {
      return blob;
    }

    operator const T *() const
    {
      return blob;
    }

  private:
    inline void free()
    {
      if (blob != nullptr)
	{
	  ::free ((void *)blob);
	  blob = nullptr;
	}
    }

  private:
    T *blob;
};

#endif // LOG_RECOVERY_UTIL_HPP
