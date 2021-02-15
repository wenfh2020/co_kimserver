#ifndef __NODE_CONNECTION_H__
#define __NODE_CONNECTION_H__

#include "connection.h"
#include "net.h"
#include "net/anet.h"
#include "protobuf/proto/msg.pb.h"
#include "server.h"

#define MAX_CONN_CNT 1

namespace kim {

class NodeConn : Logger {
   public:
    /* cmd task. */
    typedef struct task_s {
        stCoRoutine_t* co;     /* user's coroutine. */
        std::string node_type; /* dest node type. */
        std::string obj;       /* obj for node hash. */
        MsgHead* head_in;      /* request msg head. */
        MsgBody* body_in;      /* request msg body. */
        int ret;               /* result. */
        MsgHead* head_out;     /* response msg head. */
        MsgBody* body_out;     /* response msg body. */
    } task_t;

    /* coroutine's arg data.  */
    typedef struct co_data_s {
        std::string node_type;     /* node type in system. */
        std::string host;          /* node host. */
        int port;                  /* node port. */
        int worker_index;          /* node worker index. */
        Connection* c;             /* connection. */
        stCoCond_t* cond;          /* coroutine cond. */
        stCoRoutine_t* co;         /* redis conn's coroutine. */
        std::queue<task_t*> tasks; /* tasks wait to be handled. */
        void* privdata;            /* user's data. */
    } co_data_t;

    /* connections to node. */
    typedef struct node_conn_data_s {
        int max_co_cnt = MAX_CONN_CNT;
        std::vector<co_data_t*> coroutines;
    } node_conn_data_t;

   public:
    NodeConn(INet* net, Log* log);
    virtual ~NodeConn();
    void destory();

    /**
     * @brief send data from cur node to others.
     * 
     * @param node_type: gate, logic, ... which fill in config.json --> "node_type"
     * @param obj: obj for hash to get the right node.
     * @param head_in: packet head, which send to obj node.
     * @param body_in: packet data, which send to obj node.
     * @param head_out: ack msg head, recv from obj node.
     * @param body_out: ack msg body, recv from obj node.
     * 
     * @return error.h / enum E_ERROR.
     */
    int relay_to_node(const std::string& node_type, const std::string& obj,
                      MsgHead* head_in, MsgBody* body_in, MsgHead* head_out, MsgBody* body_out);

   protected:
    co_data_t* get_co_data(const std::string& node, const std::string& obj);
    static void* co_handle_task(void* arg);
    void* handle_task(void* arg);
    void clear_co_tasks(co_data_t* co_data);
    void co_sleep(int ms, int fd = -1, int events = 0);

    int handle_sys_message(Connection* c);
    int recv_data(Connection* c, MsgHead* head, MsgBody* body);
    Connection* auto_connect(const std::string& host, int port, int worker_index);
    Connection* node_connect(const std::string& node_type, const std::string& host, int port, int worker_index);

   private:
    INet* m_net = nullptr;
    char m_errstr[ANET_ERR_LEN]; /* error string. */
    std::unordered_map<std::string, node_conn_data_t*> m_coroutines;
};

}  // namespace kim

#endif  //__NODE_CONNECTION_H__