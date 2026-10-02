/* The title's binding table provides most of libc++ as runtime symbols, but
 * not basic_stringbuf<char>::str(const basic_string&): it is inline in
 * <sstream>, suppressed everywhere by `extern template class basic_stringbuf<char>`.
 * An explicit instantiation definition forces emission locally, with the
 * header's own body, so no out-of-tree reimplementation is needed. */
#include <sstream>
#include <string>

template void std::basic_stringbuf<char>::str(const std::basic_string<char>&);
