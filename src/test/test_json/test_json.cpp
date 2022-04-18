#include "../common/common.h"
#include "util/json/CJsonObject.hpp"

int main(int argc, char** argv) {
    if (!load_config(CONFIG_PATH)) {
        std::cerr << "load config failed!" << std::endl;
        return 1;
    }

    bool ret = false;
    g_config.Get("is_reuseport", ret);
    std::cout << "ret: " << ret << std::endl;
    std::cout << "data: " << g_config("is_reuseport") << std::endl;

    if (g_config.Get("zookeeper").Get("is_open", ret)) {
        std::cout << "ret: " << ret << std::endl;
        std::cout << "data: " << typeid(g_config["zookeeper"]("is_open")).name() << std::endl;
    }
    return 0;
}