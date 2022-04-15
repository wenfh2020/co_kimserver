#pragma once

#include <mysql/errmsg.h>
#include <mysql/mysql.h>

#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace kim {

using MapRow = std::unordered_map<std::string, std::string>;
using VecMapRow = std::vector<MapRow>;

class MysqlResult {
   public:
    MysqlResult() {}
    MysqlResult(MYSQL* mysql, MYSQL_RES* res);
    MysqlResult(const MysqlResult&) = delete;
    MysqlResult& operator=(const MysqlResult&) = delete;
    virtual ~MysqlResult() {}

    bool init(MYSQL* mysql, MYSQL_RES* res);

    MYSQL_ROW fetch_row();
    unsigned int num_rows();
    const MYSQL_RES* result() { return m_res; }
    int fetch_result_rows(std::shared_ptr<VecMapRow> rows);
    unsigned long* fetch_lengths();
    unsigned int fetch_num_fields();

    bool is_ok() { return m_error == 0; }
    int error() { return m_error; }
    const std::string& errstr() const { return m_errstr; }

   private:
    int m_error = -1;
    std::string m_errstr;

    MYSQL* m_mysql = nullptr;
    MYSQL_RES* m_res = nullptr;
    MYSQL_ROW m_cur_row;
    int m_row_cnt = 0;
    int m_field_cnt = 0;
};

}  // namespace kim
