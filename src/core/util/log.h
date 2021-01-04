#ifndef __KIM_LOG_H__
#define __KIM_LOG_H__

#include <stdarg.h>
#include <stdio.h>

#include <iostream>

namespace kim {

class Log {
   public:
    enum {
        LL_EMERG = 0, /* system is unusable */
        LL_ALERT,     /* action must be taken immediately */
        LL_CRIT,      /* critical conditions */
        LL_ERR,       /* error conditions */
        LL_WARNING,   /* warning conditions */
        LL_NOTICE,    /* normal but significant condition */
        LL_INFO,      /* informational */
        LL_DEBUG,     /* debug-level messages */
        LL_TRACE,     /* trace-level messages */
        LL_COUNT
    };

    Log();
    virtual ~Log() {}

   public:
    bool set_level(int level);
    bool set_level(const char* level);
    bool set_log_path(const char* path);
    bool log_data(const char* file_name,
                  int file_line, const char* func_name, int level, const char* fmt, ...);

    void set_worker_index(int index) { m_worker_index = index; }
    void set_process_type(bool is_manager) { m_is_manager = is_manager; }

   private:
    bool log_raw(const char* file_name, int file_line, const char* func_name, int level, const char* msg);

   private:
    int m_cur_level;
    std::string m_path;

    /* process info. */
    int m_worker_index = -1;
    bool m_is_manager = false;
};

class Logger {
   public:
    Logger(Log* logger) : m_logger(logger) {}
    virtual ~Logger() {}

    Log* logger() { return m_logger; }
    void set_logger(Log* logger) { m_logger = logger; }

   protected:
    Log* m_logger = nullptr;
};

}  // namespace kim

#endif  //__KIM_LOG_H__
