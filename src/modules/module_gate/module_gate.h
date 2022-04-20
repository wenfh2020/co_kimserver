#ifndef __MODULE_GATE_H__
#define __MODULE_GATE_H__

#include "module.h"

namespace kim {

class MoudleGate : public Module {
    REGISTER_HANDLER(MoudleGate)

   public:
    virtual int filter_request(std::shared_ptr<Msg> req);
};

}  // namespace kim

#endif  //__MODULE_GATE_H__