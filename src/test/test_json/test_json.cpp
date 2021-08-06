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
    return 0;
}