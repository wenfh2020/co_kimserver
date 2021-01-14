#ifndef __KIM_MYSQL_RESULT_H__
#define __KIM_MYSQL_RESULT_H__

#include <mysql/mysql.h>
#include <mysql/errmsg.h>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace kim {

typedef std::unordered_map<std::string, std::string> map_row_t;
typedef std::vector<map_row_t> vec_row_t;

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
    const MYSQL_RES* result() {
        return m_res;
    }
    int result_data(vec_row_t& data);
    unsigned long* fetch_lengths();
    unsigned int fetch_num_fields();

    bool is_ok() {
        return m_error == 0;
    }
    int error() {
        return m_error;
    }
    const std::string& errstr() const {
        return m_errstr;
    }

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

#endif  //__KIM_MYSQL_RESULT_H__
