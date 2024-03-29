#include "mysql_result.h"

namespace kim {

MysqlResult::MysqlResult(MYSQL *mysql, MYSQL_RES *res) {
    init(mysql, res);
}

bool MysqlResult::init(MYSQL *mysql, MYSQL_RES *res) {
    if (mysql == nullptr || res == nullptr) {
        m_error = -1;
        m_errstr = "invalid param!";
        return false;
    }
    m_res = res;
    m_mysql = mysql;
    m_error = mysql_errno(m_mysql);
    m_errstr = mysql_error(m_mysql);
    m_row_cnt = mysql_num_rows(m_res);
    m_field_cnt = mysql_num_fields(m_res);
    return true;
}

int MysqlResult::fetch_result_rows(std::shared_ptr<VecMapRow> rows) {
    if (m_res == nullptr) {
        return 0;
    }
    auto fields = mysql_fetch_fields(m_res);
    while ((m_cur_row = mysql_fetch_row(m_res)) != nullptr) {
        MapRow cols;
        for (int i = 0; i < m_field_cnt; i++) {
            cols.emplace(fields[i].name, (m_cur_row[i] != nullptr) ? m_cur_row[i] : "");
        }
        rows->push_back(std::move(cols));
    }
    return rows->size();
}

MYSQL_ROW MysqlResult::fetch_row() {
    if (m_res == nullptr) {
        return nullptr;
    }
    m_cur_row = mysql_fetch_row(m_res);
    return m_cur_row;
}

unsigned int MysqlResult::num_rows() {
    if (m_res == nullptr) {
        return 0;
    }
    return mysql_num_rows(m_res);
}

unsigned long *MysqlResult::fetch_lengths() {
    if (m_res == nullptr) {
        return nullptr;
    }
    // Get column lengths of the current row.
    return mysql_fetch_lengths(m_res);
}

unsigned int MysqlResult::fetch_num_fields() {
    if (m_res == nullptr) {
        return 0;
    }
    return mysql_num_fields(m_res);
}

}  // namespace kim
