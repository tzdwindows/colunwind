#ifndef COLUNWIND_EXPORT_H
#define COLUNWIND_EXPORT_H

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(COLUNWIND_BUILD_SHARED)
    #if defined(COLUNWIND_EXPORTS)
      #define COLUNWIND_API __declspec(dllexport)
    #else
      #define COLUNWIND_API __declspec(dllimport)
    #endif
  #else
    #define COLUNWIND_API
  #endif
#else
  #if defined(__GNUC__) && __GNUC__ >= 4
    #define COLUNWIND_API __attribute__((visibility("default")))
  #else
    #define COLUNWIND_API
  #endif
#endif

#endif /* COLUNWIND_EXPORT_H */
