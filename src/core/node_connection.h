#ifndef __NODE_CONNECTION_H__
#define __NODE_CONNECTION_H__

#include "connection.h"
#include "net.h"
#include "net/anet.h"
#include "protobuf/proto/msg.pb.h"
#include "server.h"

namespace kim {

class NodeConn : public Logger, public Net {
    /* cmd task. */
    typedef struct task_s {
        stCoRoutine_t* co = nullptr; /* user's coroutine. */
        std::string node_type;       /* dest node type. */
        std::string obj;             /* obj for node hash. */
        std::shared_ptr<Msg> req = nullptr;
        int ret = ERR_FAILED; /* result. */
        std::shared_ptr<Msg> ack = nullptr;
    } task_t;

    /* coroutine's arg data.  */
    typedef struct co_data_s {
        std::string node_type;                     /* node type in system. */
        std::string host;                          /* node host. */
        int port = 0;                              /* node port. */
        int worker_index = -1;                     /* node worker index. */
        std::shared_ptr<Connection> c = nullptr;   /* connection. */
        stCoCond_t* cond = nullptr;                /* coroutine cond. */
        stCoRoutine_t* co = nullptr;               /* redis conn's coroutine. */
        std::queue<std::shared_ptr<task_t>> tasks; /* tasks wait to be handled. */
    } co_data_t;

    /* connections to node. */
    typedef struct co_array_data_s {
        int max_co_cnt;
        std::vector<std::shared_ptr<co_data_t>> coroutines;
    } co_array_data_t;

   public:
    NodeConn(std::shared_ptr<INet> net, std::shared_ptr<Log> logger);
    virtual ~NodeConn();
    void destroy();

    /**
     * @brief send data from cur node to others.
     *
     * @param node_type: gate, logic, ... which fill in config.json --> "node_type"
     * @param obj: obj for hash to get the right node.
     * @param head_in: packet head, which send to obj node.
     * @param body_in: packet body, which send to obj node.
     * @param head_out: ack msg head, recv from obj node.
     * @param body_out: ack msg body, recv from obj node.
     *
     * @return error.h / enum E_ERROR.
     */
    int relay_to_node(const std::string& node_type, const std::string& obj, std::shared_ptr<Msg> req, std::shared_ptr<Msg> ack);

   protected:
    std::shared_ptr<co_data_t> get_co_data(const std::string& node_type, const std::string& obj);
    void on_handle_task(std::shared_ptr<co_data_t> cd);
    void clear_co_tasks(std::shared_ptr<co_data_t> cd);

    /* for nodes connect. */
    int handle_sys_message(std::shared_ptr<Connection> c);
    int recv_data(std::shared_ptr<Connection> c, std::shared_ptr<Msg> msg);
    std::shared_ptr<Connection> auto_connect(const std::string& host, int port, int worker_index);
    std::shared_ptr<Connection> node_connect(const std::string& node_type, const std::string& host, int port, int worker_index);

   private:
    char m_errstr[ANET_ERR_LEN]; /* error string. */
    std::unordered_map<std::string, std::shared_ptr<co_array_data_t>> m_coroutines;
};

}  // namespace kim

#endif  //__NODE_CONNECTION_H__