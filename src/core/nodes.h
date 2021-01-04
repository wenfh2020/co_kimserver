#ifndef __KIM_NODES_H__
#define __KIM_NODES_H__

#include "protobuf/sys/nodes.pb.h"
#include "server.h"
#include "util/log.h"

namespace kim {

/* 
 * nodes manager. 
 * node id: "ip:port.worker_index"
 */

typedef struct node_s {
    std::string id;               /* node id: "ip:port.worker_index" */
    std::string type;             /* node type. */
    std::string host;             /* node host. */
    int port;                     /* node port. */
    int worker_index;             /* worker index. */
    std::vector<uint32_t> vnodes; /* virtual nodes which point to me. */
} node_t;

class Nodes {
   public:
    enum class HASH_ALGORITHM {
        FNV1A_64 = 0,
        FNV1_64 = 1,
        MURMUR3_32 = 2,
    };

   public:
    Nodes(Log* logger, int vnode_cnt = 200, HASH_ALGORITHM ha = HASH_ALGORITHM::FNV1A_64);
    virtual ~Nodes();

    /* nodes. */
    bool add_zk_node(const zk_node& znode);
    bool del_zk_node(const std::string& path);
    void get_zk_diff_nodes(const std::string& type, std::vector<std::string>& in,
                           std::vector<std::string>& adds, std::vector<std::string>& dels);

    std::string get_my_zk_node_path() { return m_my_zk_node; }
    void set_my_zk_node_path(const std::string& path) { m_my_zk_node = path; }
    const std::unordered_map<std::string, zk_node>& get_zk_nodes() const { return m_zk_nodes; }

    void clear();
    uint32_t version() { return m_version; }
    void print_debug_nodes_info();

    /* ketama algorithm for node's distribution. */
    int get_node_worker_index(const std::string& node_id);
    node_t* get_node_in_hash(const std::string& node_type, int obj);
    node_t* get_node_in_hash(const std::string& node_type, const std::string& obj);

   protected:
    bool is_valid_zk_node(const zk_node& znode);
    bool check_zk_node_host(const zk_node& cur);
    bool add_node(const std::string& node_type, const std::string& ip, int port, int worker_index);
    bool del_node(const std::string& node_id);
    uint32_t hash(const std::string& obj);
    std::vector<uint32_t> gen_vnodes(const std::string& node_id);

   private:
    Log* m_logger = nullptr;
    int m_vnode_cnt = 200;
    HASH_ALGORITHM m_ha = HASH_ALGORITHM::FNV1A_64;

    uint32_t m_version = 0;
    std::string m_my_zk_node;

    /* key: host (ip:port), value: zk path. */
    std::unordered_map<std::string, std::string> m_host_zk_paths;

    /* key: zk path, value: zk node info. */
    std::unordered_map<std::string, zk_node> m_zk_nodes;
    /* key: node_id (ip:port.worker_index), value: node info. */
    std::unordered_map<std::string, node_t*> m_nodes;

    /* key: vnode(hash) -> node. */
    typedef std::map<uint32_t, node_t*> VNODE2NODE_MAP;
    /* key: node type, value: (vnodes -> node) */
    std::unordered_map<std::string, VNODE2NODE_MAP> m_vnodes;
};

}  // namespace kim

#endif  //__KIM_NODES_H__