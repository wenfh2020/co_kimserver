
#include "server.h"

#include <fcntl.h>

#include "manager.h"
#include "util/set_proc_title.h"
#include "util/util.h"

// #define DAEMONSIZE

void init_server(int argc, char** argv) {
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    spt_init(argc, argv);
#ifdef DAEMONSIZE
    daemonize();
#endif
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "invalid param num!" << std::endl;
        exit(EXIT_FAIL);
    }

    init_server(argc, argv);

    kim::Manager mgr;
    if (!mgr.init(argv[1])) {
        std::cerr << "init manager failed!" << std::endl;
        exit(EXIT_FAIL);
    }
    mgr.run();
    return 0;
}