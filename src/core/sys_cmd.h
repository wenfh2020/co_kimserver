#ifndef __KIM_SYS_CMD_H__
#define __KIM_SYS_CMD_H__

#include "net.h"
#include "protobuf/sys/payload.pb.h"
#include "request.h"
#include "server.h"

namespace kim {

class SysCmd : Logger {
   public:
    SysCmd(Log* logger, INet* net);
    virtual ~SysCmd() {}

    int send_payload_to_manager(const Payload& pl);

    /* communication between nodes.  */
    int handle_msg(const Request* req);

   private:
    int handle_worker_msg(const Request* req);
    int handle_manager_msg(const Request* req);

    int on_req_update_payload(const Request* req);
    int on_rsp_update_payload(const Request* req);

   private:
    int check_rsp(const Request* req);

   protected:
    INet* m_net = nullptr;
    int m_timer_index = 0;
};

}  // namespace kim

#endif  //__KIM_SYS_CMD_H__