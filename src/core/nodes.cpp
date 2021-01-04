#include "nodes.h"

#include "util/hash.h"
#include "util/util.h"

namespace kim {

Nodes::Nodes(Log* logger, int vnode_cnt, HASH_ALGORITHM ha)
    : m_logger(logger), m_vnode_cnt(vnode_cnt), m_ha(ha) {
}

Nodes::~Nodes() {
    clear();
}

bool Nodes::is_valid_zk_node(const zk_node& znode) {
    return !(znode.path().empty() || znode.host().empty() || znode.port() == 0 ||
             znode.type().empty() || znode.worker_cnt() == 0 || znode.active_time() == 0);
}

bool Nodes::check_zk_node_host(const zk_node& cur) {
    std::string host = format_str("%s:%d", cur.host().c_str(), cur.port());
    auto it = m_host_zk_paths.find(host);
    if (it == m_host_zk_paths.end()) {
        return true;
    }

    auto znode_it = m_zk_nodes.find(it->second);
    if (znode_it == m_zk_nodes.end()) {
        m_host_zk_paths.erase(it);
        return true;
    }

    const zk_node& old = znode_it->second;
    if (cur.path() == old.path()) {
        return true;
    }

    /* the old can not replace the new. */
    if (cur.active_time() <= old.active_time()) {
        LOG_WARN("cur znode is old, old path: %s, cur path: %s",
                 old.path().c_str(), cur.path().c_str());
        return false;
    } else {
        /* delete old. */
        del_zk_node(old.path());
        LOG_INFO("del old zk node, path: %s", old.path().c_str());
        return true;
    }
}

bool Nodes::add_zk_node(const zk_node& znode) {
    if (!is_valid_zk_node(znode)) {
        LOG_ERROR(
            "add zk znode failed, invalid znode data! "
            "znode path: %s, host: %s,  port: %d, type: %s, worker_cnt: %d",
            znode.path().c_str(), znode.host().c_str(), znode.port(),
            znode.type().c_str(), znode.worker_cnt());
        return false;
    }

    if (!check_zk_node_host(znode)) {
        return false;
    }

    auto it = m_zk_nodes.find(znode.path());
    if (it == m_zk_nodes.end()) {
        m_zk_nodes[znode.path()] = znode;
        for (size_t i = 1; i <= znode.worker_cnt(); i++) {
            add_node(znode.type(), znode.host(), znode.port(), i);
        }
    } else {
        const zk_node& old = it->second;
        if (old.SerializeAsString() != znode.SerializeAsString()) {
            m_zk_nodes[znode.path()] = znode;
            for (size_t i = 1; i < old.worker_cnt(); i++) {
                del_node(format_nodes_id(old.host(), old.port(), i));
            }
            for (size_t i = 1; i <= znode.worker_cnt(); i++) {
                add_node(znode.type(), znode.host(), znode.port(), i);
            }
            LOG_DEBUG("update zk znode info done! path: %s", znode.path().c_str());
        }
    }

    std::string host = format_str("%s:%d", znode.host().c_str(), znode.port());
    m_host_zk_paths[host] = znode.path();
    return true;
}

bool Nodes::del_zk_node(const std::string& path) {
    auto it = m_zk_nodes.find(path);
    if (it == m_zk_nodes.end()) {
        return false;
    }

    /* delete nodes. */
    const zk_node& znode = it->second;
    for (size_t i = 1; i <= znode.worker_cnt(); i++) {
        del_node(format_nodes_id(znode.host(), znode.port(), i));
    }

    /* delete host & path info. */
    std::string host = format_str("%s:%d", znode.host().c_str(), znode.port());
    auto itr = m_host_zk_paths.find(host);
    if (itr != m_host_zk_paths.end()) {
        m_host_zk_paths.erase(itr);
    }

    LOG_INFO("delete zk node, path: %s, type: %s, host: %s, port: %d, worker_cnt: %d",
             znode.path().c_str(), znode.type().c_str(),
             znode.host().c_str(), znode.port(), znode.worker_cnt());
    m_zk_nodes.erase(it);
    return true;
}

void Nodes::get_zk_diff_nodes(const std::string& type, std::vector<std::string>& in,
                              std::vector<std::string>& adds, std::vector<std::string>& dels) {
    std::vector<std::string> vec;
    for (auto& v : m_zk_nodes) {
        if (type == v.second.type()) {
            vec.push_back(v.first);
        }
    }

    adds = diff_cmp(in, vec);
    dels = diff_cmp(vec, in);
    LOG_DEBUG("cur nodes size: %lu, add size: %lu, dels size: %lu",
              vec.size(), adds.size(), dels.size());
}

bool Nodes::add_node(const std::string& node_type, const std::string& host, int port, int worker) {
    LOG_INFO("add node, node type: %s, host: %s, port: %d, worker: %d",
             node_type.c_str(), host.c_str(), port, worker);

    std::string node_id = format_nodes_id(host, port, worker);
    if (m_nodes.find(node_id) != m_nodes.end()) {
        LOG_DEBUG("node (%s) has been added!", node_id.c_str());
        return true;
    }

    m_version++;

    node_t* node;
    std::vector<uint32_t> vnodes;
    VNODE2NODE_MAP& vnode2node = m_vnodes[node_type];
    size_t old_vnode_cnt = vnode2node.size();

    vnodes = gen_vnodes(node_id);
    node = new node_t{node_id, node_type, host, port, worker, vnodes};

    for (auto& v : vnodes) {
        if (!vnode2node.insert({v, node}).second) {
            LOG_WARN(
                "duplicate virtual nodes! "
                "vnode: %lu, node type: %s, host: %s, port: %d, worker: %d.",
                v, node_type.c_str(), host.c_str(), port, worker);
            continue;
        }
    }

    if (vnode2node.size() == old_vnode_cnt) {
        LOG_ERROR("add virtual nodes failed! node id: %s, node type: %s",
                  node->id.c_str(), node->type.c_str());
        SAFE_DELETE(node);
        return false;
    }

    m_nodes[node_id] = node;
    return true;
}

bool Nodes::del_node(const std::string& node_id) {
    LOG_DEBUG("delete node: %s", node_id.c_str());

    auto it = m_nodes.find(node_id);
    if (it == m_nodes.end()) {
        return false;
    }

    m_version++;

    /* clear vnode. */
    node_t* node = it->second;
    auto itr = m_vnodes.find(node->type);
    if (itr != m_vnodes.end()) {
        for (auto& v : node->vnodes) {
            itr->second.erase(v);
        }
    }

    delete node;
    m_nodes.erase(it);
    return true;
}

int Nodes::get_node_worker_index(const std::string& node_id) {
    LOG_TRACE("get node worker index, node id: %s", node_id.c_str());
    auto it = m_nodes.find(node_id);
    return (it != m_nodes.end()) ? it->second->worker_index : -1;
}

node_t* Nodes::get_node_in_hash(const std::string& node_type, int obj) {
    return get_node_in_hash(node_type, std::to_string(obj));
}

node_t* Nodes::get_node_in_hash(const std::string& node_type, const std::string& obj) {
    auto it = m_vnodes.find(node_type);
    if (it == m_vnodes.end()) {
        return nullptr;
    }

    uint32_t hash_key = hash(obj);
    const VNODE2NODE_MAP& vnode2node = it->second;
    if (vnode2node.size() == 0) {
        LOG_WARN(
            "can't not find node in virtual nodes. node type: %s, obj: %s, hash key: %lu",
            node_type.c_str(), obj.c_str(), hash_key);
        return nullptr;
    }

    auto itr = vnode2node.lower_bound(hash_key);
    if (itr == vnode2node.end()) {
        itr = vnode2node.begin();
    }
    return itr->second;
}

uint32_t Nodes::hash(const std::string& obj) {
    if (m_ha == HASH_ALGORITHM::FNV1_64) {
        return hash_fnv1_64(obj.c_str(), obj.size());
    } else if (m_ha == HASH_ALGORITHM::MURMUR3_32) {
        return murmur3_32(obj.c_str(), obj.size(), 0x000001b3);
    } else {
        return hash_fnv1a_64(obj.c_str(), obj.size());
    }
}

std::vector<uint32_t> Nodes::gen_vnodes(const std::string& node_id) {
    std::string s;
    int hash_point = 4;
    std::vector<uint32_t> vnodes;

    for (int i = 0; i < m_vnode_cnt / hash_point; i++) {
        s = md5(format_str("%d@%s#%d", m_vnode_cnt - i, node_id.c_str(), i));
        for (int j = 0; j < hash_point; j++) {
            uint32_t v = ((uint32_t)(s[3 + j * hash_point] & 0xFF) << 24) |
                         ((uint32_t)(s[2 + j * hash_point] & 0xFF) << 16) |
                         ((uint32_t)(s[1 + j * hash_point] & 0xFF) << 8) |
                         (s[j * hash_point] & 0xFF);
            vnodes.push_back(v);
        }
    }
    return vnodes;
}

void Nodes::print_debug_nodes_info() {
    LOG_DEBUG("------------");
    /* host zk path */
    LOG_DEBUG("host zk path (%lu):", m_host_zk_paths.size());
    for (auto& v : m_host_zk_paths) {
        LOG_DEBUG("host: %s, zk path: %s", v.first.c_str(), v.second.c_str());
    }

    /* zk nodes */
    LOG_DEBUG("zk nodes (%lu):", m_zk_nodes.size());
    for (auto& v : m_zk_nodes) {
        const zk_node& znode = v.second;
        LOG_DEBUG("zk node type: %s, path: %s, host: %s, port: %d, wc: %d",
                  znode.type().c_str(), znode.path().c_str(),
                  znode.host().c_str(), znode.port(), znode.worker_cnt());
    }

    /* vnodes */
    LOG_DEBUG("vnodes(%lu):", m_vnodes.size());
    for (auto& v : m_vnodes) {
        LOG_DEBUG("type: %s, vnode size: %lu", v.first.c_str(), v.second.size());
    }

    /* nodes */
    LOG_DEBUG("nodes (%lu):", m_nodes.size());
    for (auto& v : m_nodes) {
        node_t* node = v.second;
        LOG_DEBUG("id: %s, node type: %s, host: %s, port: %d, worker index: %d",
                  v.first.c_str(), node->type.c_str(),
                  node->host.c_str(), node->port, node->worker_index);
    }
    LOG_DEBUG("------------");
}

void Nodes::clear() {
    m_my_zk_node.clear();
    m_host_zk_paths.clear();
    m_zk_nodes.clear();
    for (auto& it : m_nodes) {
        delete it.second;
    }
    m_nodes.clear();
    m_vnodes.clear();
}

}  // namespace kim
