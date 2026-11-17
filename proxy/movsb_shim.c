/* MinHook's trampoline.c calls the MSVC intrinsic __movsb when _MSC_VER is defined.
   clang driven in GNU mode (clang++ + lld-link, MSVC target) defines _MSC_VER but leaves
   __movsb as an unresolved external. Provide it. (Not compiled for MSVC / clang-cl / MinGW.) */
#include <stddef.h>
#include <string.h>
void __movsb(unsigned char* dst, unsigned char const* src, size_t n) { memcpy(dst, src, n); }
