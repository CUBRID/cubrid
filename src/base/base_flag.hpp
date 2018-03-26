/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * base_flag.hpp - template for flagged fields
 */

#ifndef _BASE_FLAG_HPP_
#define _BASE_FLAG_HPP_

template <typename T>
class flag
{
  public:
    inline flag ();
    inline flag (const flag<T> &other);
    inline flag (const T &init_flags);

    inline flag &operator= (const flag<T> &other);
    inline flag &operator= (const T &flags);

    inline flag &set (const T &flags);
    inline flag &clear (const T &flags);
    inline bool is_set (const T &flags);
    inline bool is_all_set (const T &flags);
    inline bool is_any_set (const T &flags);
    inline T get_flags (void);

    static inline void set_flag (T &where_to_set, const T &what_to_set);
    static inline void clear_flag (T &where_to_clear, const T &what_to_clear);
    static inline bool is_flag_set (const T &where_to_check, const T &what_to_check);
    static inline bool is_flag_all_set (const T &where_to_check, const T &what_to_check);
    static inline bool is_flag_any_set (const T &where_to_check, const T &what_to_check);

  private:
    T m_flags;
};

template<typename T>
inline
flag<T>::flag ()
{
  m_flags = 0;
}

template<typename T>
inline flag<T>::flag (const flag &other)
{
  m_flags = other.m_flags;
}

template<typename T>
inline
flag<T>::flag (const T &init_flags)
{
  m_flags = init_flags;
}

template<typename T>
inline flag<T> &
flag<T>::operator= (const flag &other)
{
  this->m_flags = other.m_flags;
  return *this;
}

template<typename T>
inline flag<T> &
flag<T>::operator= (const T &flags)
{
  this->m_flags = flags;
  return *this;
}

template<typename T>
inline flag<T> &
flag<T>::set (const T &flags)
{
  m_flags = m_flags | flags;
  return *this;
}

template<typename T>
inline flag<T> &
flag<T>::clear (const T &flags)
{
  m_flags = m_flags & (~flags);
  return *this;
}

template<typename T>
inline bool
flag<T>::is_set (const T &flags)
{
  /* sugar syntax */
  return is_any_set (flags);
}

template<typename T>
inline bool flag<T>::is_all_set (const T &flags)
{
  return (m_flags & flags) == flags;
}

template<typename T>
inline bool flag<T>::is_any_set (const T &flags)
{
  return (m_flags & flags) != 0;
}

template<typename T>
inline T
flag<T>::get_flags (void)
{
  return m_flags;
}

template<typename T>
inline void
flag<T>::set_flag (T &where_to_set, const T &what_to_set)
{
  where_to_set = where_to_set | what_to_set;
}

template<typename T>
inline void
flag<T>::clear_flag (T &where_to_clear, const T &what_to_clear)
{
  where_to_clear = where_to_clear & (~what_to_clear);
}

template<typename T>
inline bool
flag<T>::is_flag_set (const T &where_to_check, const T &what_to_check)
{
  /* syntax sugar */
  return flag<T>::is_flag_any_set (where_to_check, what_to_check);
}

template<typename T>
inline bool flag<T>::is_flag_all_set (const T &where_to_check, const T &what_to_check)
{
  return (where_to_check & what_to_check) == what_to_check;
}

template<typename T>
inline bool flag<T>::is_flag_any_set (const T &where_to_check, const T &what_to_check)
{
  return (where_to_check & what_to_check) != 0;
}

#endif /* _BASE_FLAG_HPP_ */

