#include "zk.h"
#include <string.h>

namespace utility {

static const int32_t zoo_path_buf_len = 1024;
static const int32_t zoo_value_buf_len = 10240;
static const int32_t zoo_recv_time_out = 10000;  // in milliseconds

static FILE* m_zk_log_file = nullptr;

namespace details {

static const char* state_to_string(int state) {
    if (state == 0)
        return "zoo_closed_state";
    if (state == ZOO_CONNECTING_STATE)
        return "zoo_state_connecting";
    if (state == ZOO_ASSOCIATING_STATE)
        return "zoo_state_associating";
    if (state == ZOO_CONNECTED_STATE)
        return "zoo_state_connected";
    if (state == ZOO_EXPIRED_SESSION_STATE)
        return "zoo_state_expired_session";
    if (state == ZOO_AUTH_FAILED_STATE)
        return "zoo_state_auth_failed";

    return "zoo_state_invalid";
}

static const char* type_to_string(int state) {
    if (state == ZOO_CREATED_EVENT)
        return "zoo_event_created";
    if (state == ZOO_DELETED_EVENT)
        return "zoo_event_deleted";
    if (state == ZOO_CHANGED_EVENT)
        return "zoo_event_changed";
    if (state == ZOO_CHILD_EVENT)
        return "zoo_event_child";
    if (state == ZOO_SESSION_EVENT)
        return "zoo_event_session";
    if (state == ZOO_NOTWATCHING_EVENT)
        return "zoo_event_notwatching";

    return "zoo_event_unknow";
}

static void default_void_completion_func(int rc, const void* data) {
    // printf("default_void_completion_func, rc = %d, data: %p\n", rc, data);
}

static void state_to_zoo_state_t(const struct Stat& s, zoo_state_t* state) {
    state->ctime = s.ctime;
    state->mtime = s.mtime;
    state->version = s.version;
    state->children_count = s.numChildren;
}
}  // namespace details

zk_cpp::zk_cpp() : m_zh(0) {
}

zk_cpp::~zk_cpp() {
    close();
    if (m_zk_log_file != nullptr) {
        fclose(m_zk_log_file);
        m_zk_log_file = nullptr;
    }
}

void zk_cpp::close() {
    if (m_zh) {
        zookeeper_close((zhandle_t*)m_zh);
        m_zh = NULL;
    }
}

const char* zk_cpp::error_string(int32_t rc) {
    return zerror(rc);
}

const char* zk_cpp::state_to_string(int32_t state) {
    return details::state_to_string(state);
}

void zk_cpp::set_log_lvl(zoo_log_lvl lvl) {
    zoo_set_debug_level((ZooLogLevel)lvl);
}

bool zk_cpp::set_log_stream(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "a");
    if (fp == nullptr) {
        return false;
    }

    zoo_set_log_stream(fp);

    if (m_zk_log_file != nullptr) {
        fclose(m_zk_log_file);
        m_zk_log_file = nullptr;
    }
    m_zk_log_file = fp;
    return true;
}

zoo_acl_t zk_cpp::create_world_acl(int32_t perms) {
    zoo_acl_t acl("world", "anyone", perms);
    return acl;
}

zoo_acl_t zk_cpp::create_auth_acl(int32_t perms) {
    zoo_acl_t acl("auth", "", perms);
    return acl;
}

zoo_acl_t zk_cpp::create_digest_acl(int32_t perms, const std::string& user, const std::string& passwd) {
    std::string id = user;
    id.append(":").append(passwd);
    zoo_acl_t acl("digest", id.c_str(), perms);
    return acl;
}

zoo_acl_t zk_cpp::create_ip_acl(int32_t perms, const std::string& ip_info) {
    zoo_acl_t acl("ip", ip_info.c_str(), perms);
    return acl;
}

zoo_rc zk_cpp::connect(const std::string& url, watcher_fn fn, void* privdata) {
    m_url = url;

    /* try close first */
    close();

    /* init zk. */
    m_zh = zookeeper_init(
        url.c_str(), fn, zoo_recv_time_out, nullptr, privdata, 0);
    return z_ok;
}

int32_t zk_cpp::get_recv_time_out() {
    if (m_zh != NULL) {
        return zoo_recv_timeout((zhandle_t*)m_zh);
    }
    return 0;
}

z_state zk_cpp::get_state() {
    if (m_zh != NULL) {
        return (z_state)zoo_state((zhandle_t*)m_zh);
    }
    return zoo_state_closed;
}

bool zk_cpp::unrecoverable() {
    if (m_zh != NULL) {
        return is_unrecoverable((zhandle_t*)m_zh) == z_invliad_state;
    }
    return true;
}

zoo_rc zk_cpp::create_node(const char* path, const std::string& value, const std::vector<zoo_acl_t>& acl, int32_t create_flags, char* path_buffer, int32_t path_buffer_len) {
    struct ACL_vector acl_v;
    acl_v.count = (int32_t)acl.size();
    struct ACL* acl_list = NULL;
    if (acl_v.count > 0) {
        acl_list = new struct ACL[acl_v.count];

        for (int32_t i = 0; i < (int32_t)acl.size(); ++i) {
            acl_list[i].perms = acl[i].perm;
            acl_list[i].id.scheme = (char*)acl[i].scheme.c_str();
            acl_list[i].id.id = (char*)acl[i].id.c_str();
        }
    }
    acl_v.data = acl_list;

    zoo_rc rt = (zoo_rc)zoo_create((zhandle_t*)m_zh, path, value.c_str(), (int)value.size(), &acl_v, create_flags, path_buffer, (int)path_buffer_len);

    if (acl_list != NULL) {
        delete[] acl_list;
    }

    return rt;
}

zoo_rc zk_cpp::create_persistent_node(const char* path, const std::string& value, const std::vector<zoo_acl_t>& acl) {
    return create_node(path, value, acl, 0, nullptr, 0);
}

zoo_rc zk_cpp::create_sequence_node(const char* path, const std::string& value, const std::vector<zoo_acl_t>& acl, std::string& returned_path_name) {
    char rpath[zoo_path_buf_len] = {0};
    zoo_rc rt = create_node(path, value, acl, ZOO_SEQUENCE, rpath, (int32_t)sizeof(rpath));
    returned_path_name = rpath;
    return rt;
}

zoo_rc zk_cpp::create_ephemeral_node(const char* path, const std::string& value, const std::vector<zoo_acl_t>& acl) {
    return create_node(path, value, acl, ZOO_EPHEMERAL, nullptr, 0);
}

zoo_rc zk_cpp::create_sequance_ephemeral_node(const char* path, const std::string& value, const std::vector<zoo_acl_t>& acl, std::string& returned_path_name) {
    char rpath[zoo_path_buf_len] = {0};
    zoo_rc rt = create_node(path, value, acl, ZOO_SEQUENCE | ZOO_EPHEMERAL, rpath, (int32_t)sizeof(rpath));
    returned_path_name = rpath;
    return rt;
}

zoo_rc zk_cpp::delete_node(const char* path, int32_t version) {
    return (zoo_rc)zoo_delete((zhandle_t*)m_zh, path, version);
}

zoo_rc zk_cpp::exists_node(const char* path, zoo_state_t* info, bool watch) {
    struct Stat s = {0};

    zoo_rc rt = (zoo_rc)zoo_exists((zhandle_t*)m_zh, path, (int)watch, &s);

    if (info) {
        details::state_to_zoo_state_t(s, info);
    }

    return rt;
}

zoo_rc zk_cpp::get_node(const char* path, std::string& out_value, zoo_state_t* info, bool watch) {
    struct Stat s = {0};

    char buf[zoo_value_buf_len] = {0};
    int buf_size = sizeof(buf);
    char* data_buffer = buf;
    zoo_rc rt = (zoo_rc)zoo_get((zhandle_t*)m_zh, path, watch, data_buffer, &buf_size, &s);
    do {
        if (rt != z_ok) {
            break;
        }

        // check data complete
        if (buf_size > 0) {
            if (s.dataLength > buf_size) {
                data_buffer = (char*)malloc(s.dataLength + 1);
                memset(data_buffer, 0, s.dataLength + 1);
                buf_size = s.dataLength;
                rt = (zoo_rc)zoo_get((zhandle_t*)m_zh, path, watch, data_buffer, &buf_size, &s);
                if (rt != z_ok) {
                    break;
                }
            }
            out_value = data_buffer;
        }

        if (info) {
            details::state_to_zoo_state_t(s, info);
        }
    } while (0);

    // try free buffer
    if (data_buffer != buf) {
        free(data_buffer);
    }

    return rt;
}

zoo_rc zk_cpp::set_node(const char* path, const std::string& value, int32_t version) {
    return (zoo_rc)zoo_set((zhandle_t*)m_zh, path, value.c_str(), (int)value.size(), version);
}

zoo_rc zk_cpp::get_children(const char* path, std::vector<std::string>& children, bool watch) {
    String_vector string_v;
    zoo_rc rt = (zoo_rc)zoo_get_children((zhandle_t*)m_zh, path, watch, &string_v);
    if (rt != z_ok) {
        return rt;
    }

    for (int32_t i = 0; i < string_v.count; ++i) {
        children.push_back(string_v.data[i]);
    }

    return rt;
}

zoo_rc zk_cpp::set_acl(const char* path, const std::vector<zoo_acl_t>& acl, int32_t version) {
    struct ACL_vector acl_v;
    acl_v.count = (int32_t)acl.size();
    struct ACL* acl_list = NULL;
    if (acl_v.count > 0) {
        acl_list = new struct ACL[acl_v.count];

        for (int32_t i = 0; i < (int32_t)acl.size(); ++i) {
            acl_list[i].perms = acl[i].perm;
            acl_list[i].id.scheme = (char*)acl[i].scheme.c_str();
            acl_list[i].id.id = (char*)acl[i].id.c_str();
        }
    }
    acl_v.data = acl_list;

    zoo_rc rt = (zoo_rc)zoo_set_acl((zhandle_t*)m_zh, path, version, &acl_v);

    if (acl_list != NULL) {
        delete[] acl_list;
    }

    return rt;
}

zoo_rc zk_cpp::get_acl(const char* path, std::vector<zoo_acl_t>& acl) {
    struct ACL_vector acl_v = {0};

    zoo_rc ret = (zoo_rc)zoo_get_acl((zhandle_t*)m_zh, path, &acl_v, NULL);

    if (ret == z_ok) {
        for (int32_t i = 0; i < acl_v.count; ++i) {
            acl.resize(acl.size() + 1);
            auto& acl_info = acl.back();
            acl_info.perm = acl_v.data[i].perms;
            acl_info.scheme = acl_v.data[i].id.scheme;
            acl_info.id = acl_v.data[i].id.id;
        }
    }
    return ret;
}

zoo_rc zk_cpp::add_auth(const std::string& user_name, const std::string& user_passwd) {
    std::string cert = user_name;
    cert.append(":").append(user_passwd);
    return (zoo_rc)zoo_add_auth((zhandle_t*)m_zh, "digest", cert.c_str(),
                                (int)cert.size(), details::default_void_completion_func, this);
}

zoo_rc zk_cpp::watch_data_change(const char* path, std::string& value) {
    std::string out_value;
    zoo_rc rt = get_node(path, out_value, nullptr, true);
    if (rt != z_ok) {
        return rt;
    }

    value = out_value;
    return rt;
}

zoo_rc zk_cpp::watch_children_event(const char* path, std::vector<std::string>& out_children) {
    std::vector<std::string> children;
    zoo_rc rt = get_children(path, children, true);
    if (rt != z_ok) {
        return rt;
    }

    out_children = children;
    return rt;
}

}  // namespace utility