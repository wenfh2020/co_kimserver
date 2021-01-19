#ifndef __KIM_COMMON_H__
#define __KIM_COMMON_H__

#include "./libco/co_routine.h"
#include "mysql/mysql_mgr.h"
#include "redis/redis_mgr.h"
#include "server.h"

using namespace kim;

CJsonObject g_config;
Log* m_logger = nullptr;
MysqlMgr* g_mysql_mgr = nullptr;
RedisMgr* g_redis_mgr = nullptr;

#define LOG_PATH "test.log"
#define CONFIG_PATH "../../../bin/config.json"

bool load_logger(const char* path) {
    m_logger = new Log;
    if (!m_logger->set_log_path(path)) {
        std::cerr << "set log path failed!" << std::endl;
        return false;
    }
    m_logger->set_level(Log::LL_INFO);
    m_logger->set_worker_index(0);
    m_logger->set_process_type(true);
    return true;
}

bool load_config(const std::string& path) {
    if (!g_config.Load(path)) {
        LOG_ERROR("load config failed!");
        return false;
    }
    return true;
}

bool load_mysql_mgr(Log* logger, CJsonObject& config) {
    g_mysql_mgr = new MysqlMgr(logger);
    if (!g_mysql_mgr->init(config)) {
        LOG_ERROR("load db mgr failed!");
    }
    return true;
}

bool load_redis_mgr(Log* logger, CJsonObject& config) {
    g_redis_mgr = new RedisMgr(logger);
    if (!g_redis_mgr->init(config)) {
        LOG_ERROR("load redis mgr failed!");
    }
    return true;
}

#endif  //__KIM_COMMON_H__