#ifndef LOG_RECOVERY_UTIL_HPP
#define LOG_RECOVERY_UTIL_HPP

#include <memory>

/* helper alias to RAII a malloc'ed sequence of bytes
 *
 */
template <typename T>
using raii_blob = std::unique_ptr<T, decltype (::free) *>;

#endif // LOG_RECOVERY_UTIL_HPP
