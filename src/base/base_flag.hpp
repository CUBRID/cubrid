/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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

