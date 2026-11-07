#ifndef PRJ_HTTPS_LOG_H
#define PRJ_HTTPS_LOG_H

#include <iostream>
#include <sstream>

// log level: 1 = debug, 2 = info (default), 3 = warning, 4 = error, 5 = no logging
#ifndef PRJ_HTTPS_GLOBAL_LOG_LEVEL
#define PRJ_HTTPS_GLOBAL_LOG_LEVEL 2
#endif

// end of recursion: no arguments left
inline void prjHttpsLogAppend(std::ostringstream &line)
{
    (void)line;
}

// appends " <first>" and the rest of the arguments to line
template <typename First, typename... Rest>
inline void prjHttpsLogAppend(std::ostringstream &line, const First &first, const Rest &... rest)
{
    line << ' ' << first;
    prjHttpsLogAppend(line, rest...);
}

// writes the arguments separated by spaces as one line to std::cout. The line is
// built first and written at once, so lines logged from different threads
// (e.g. download threads) do not get mixed with each other.
template <typename First, typename... Rest>
inline void prjHttpsLog(const First &first, const Rest &... rest)
{
    std::ostringstream line;
    line << first;
    prjHttpsLogAppend(line, rest...);
    line << '\n';
    std::cout << line.str() << std::flush;
}

#if PRJ_HTTPS_GLOBAL_LOG_LEVEL < 2
#define PRJ_HTTPS_DEBUG_LOG(...) prjHttpsLog("[D]", __FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)
#else
#define PRJ_HTTPS_DEBUG_LOG(...)
#endif

#if PRJ_HTTPS_GLOBAL_LOG_LEVEL < 3
#define PRJ_HTTPS_INFO_LOG(...) prjHttpsLog("[I]", __FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)
#else
#define PRJ_HTTPS_INFO_LOG(...)
#endif

#if PRJ_HTTPS_GLOBAL_LOG_LEVEL < 4
#define PRJ_HTTPS_WARNING_LOG(...) prjHttpsLog("[W]", __FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)
#else
#define PRJ_HTTPS_WARNING_LOG(...)
#endif

#if PRJ_HTTPS_GLOBAL_LOG_LEVEL < 5
#define PRJ_HTTPS_ERROR_LOG(...) prjHttpsLog("[E]", __FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)
#else
#define PRJ_HTTPS_ERROR_LOG(...)
#endif

#endif // PRJ_HTTPS_LOG_H
