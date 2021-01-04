#include "util.h"

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1

#include <cryptopp/hex.h>
#include <cryptopp/md5.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <sstream>

#define MAX_PATH 256
#define CONFIG_MIN_RESERVED_FDS 32

int str_to_int(const std::string& d) {
    return d.empty() ? 0 : std::stoi(d);
}

void split_str(const std::string& s, std::vector<std::string>& vec,
               const std::string& seq, bool trim_blank) {
    std::size_t pre = 0, cur = 0;
    while ((pre = s.find_first_not_of(seq, cur)) != std::string::npos) {
        cur = s.find(seq, pre);
        if (cur == std::string::npos) {
            vec.push_back(s.substr(pre, s.length() - pre));
            break;
        }
        vec.push_back(s.substr(pre, cur - pre));
    }

    if (trim_blank && seq != " ") {
        for (auto& v : vec) {
            v.erase(0, v.find_first_not_of(" "));
            v.erase(v.find_last_not_of(" ") + 1);
        }
    }
}

std::string format_str(const char* const fmt, ...) {
    char s[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s, sizeof(s), fmt, ap);
    va_end(ap);
    return std::string(s);
}

std::string format_addr(const std::string& host, int port) {
    char addr[64];
    snprintf(addr, sizeof(addr), "%s:%d", host.c_str(), port);
    return std::string(addr);
}

std::string format_nodes_id(const std::string& host, int port, int index) {
    char identity[128];
    snprintf(identity, sizeof(identity), "%s:%d.%d", host.c_str(), port, index);
    return std::string(identity);
}

std::string work_path() {
    char work_path[MAX_PATH] = {0};
    if (!getcwd(work_path, sizeof(work_path))) {
        return "";
    }
    return std::string(work_path);
}

std::string format_redis_cmds(const std::vector<std::string>& argv) {
    std::ostringstream oss;
    for (auto& it : argv) {
        oss << it << " ";
    }
    return oss.str();
}

std::string md5(const std::string& input) {
    std::string hash;
    CryptoPP::Weak1::MD5 m;
    CryptoPP::HexEncoder oHexEncoder;
    m.Update((unsigned char*)input.c_str(), input.length());
    hash.resize(m.DigestSize());
    m.Final((unsigned char*)&hash[0]);
    return hash;
}

bool proto_to_json(const google::protobuf::Message& message, std::string& json) {
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = false;
    options.always_print_primitive_fields = true;
    options.preserve_proto_field_names = true;
    return MessageToJsonString(message, &json, options).ok();
}

bool json_file_to_proto(const std::string& file, google::protobuf::Message& message) {
    std::ifstream is(file);
    if (!is.good()) {
        // printf("open json file failed, file: %s\n", file.c_str());
        return false;
    }
    std::stringstream json;
    json << is.rdbuf();
    is.close();
    // printf("--------\n %s\n", json.str().c_str());
    return JsonStringToMessage(json.str(), &message).ok();
}

bool json_to_proto(const std::string& json, google::protobuf::Message& message) {
    return JsonStringToMessage(json, &message).ok();
}

void daemonize(void) {
    int fd;

    if (fork() != 0) exit(0); /* parent exits */
    setsid();                 /* create a new session */

    /* Every output goes to /dev/null. If server is daemonized but
     * the 'logfile' is set to 'stdout' */
    if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
}

bool adjust_files_limit(int& max_clients) {
    rlim_t maxfiles = max_clients + CONFIG_MIN_RESERVED_FDS;
    struct rlimit limit;

    if (getrlimit(RLIMIT_NOFILE, &limit) == -1) {
        return false;
    }

    rlim_t oldlimit = limit.rlim_cur;
    if (oldlimit >= maxfiles) {
        return true;
    }

    rlim_t bestlimit;
    // int setrlimit_error = 0;

    /* Try to set the file limit to match 'maxfiles' or at least
     * to the higher value supported less than maxfiles. */
    bestlimit = maxfiles;
    while (bestlimit > oldlimit) {
        rlim_t decr_step = 16;

        limit.rlim_cur = bestlimit;
        limit.rlim_max = bestlimit;
        if (setrlimit(RLIMIT_NOFILE, &limit) != -1) {
            break;
        }
        // setrlimit_error = errno;

        /* We failed to set file limit to 'bestlimit'. Try with a
         * smaller limit decrementing by a few FDs per iteration. */
        if (bestlimit < decr_step) {
            break;
        }
        bestlimit -= decr_step;
    }

    /* Assume that the limit we get initially is still valid if
     * our last try was even lower. */
    if (bestlimit < oldlimit) {
        bestlimit = oldlimit;
    }

    if (bestlimit < maxfiles) {
        // unsigned int old_maxclients = max_clients;
        max_clients = bestlimit - CONFIG_MIN_RESERVED_FDS;
        /* maxclients is unsigned so may overflow: in order
                 * to check if maxclients is now logically less than 1
                 * we test indirectly via bestlimit. */
        if (bestlimit <= CONFIG_MIN_RESERVED_FDS) {
            return false;
        }
        return false;
    }
    return true;
}

const char* to_lower(char* s, int len) {
    for (int j = 0; j < len; j++) {
        s[j] = tolower(s[j]);
    }
    return s;
}

long long mstime() {
    struct timeval tv;
    long long mst;

    gettimeofday(&tv, NULL);
    mst = ((long long)tv.tv_sec) * 1000;
    mst += tv.tv_usec / 1000;
    return mst;
}

long long ustime() {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long)tv.tv_sec) * 1000000;
    ust += tv.tv_usec;
    return ust;
}

double time_now() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return ((tv).tv_sec + (tv).tv_usec * 1e-6);
}

double decimal_rand() {
    return double(rand()) / (double(RAND_MAX) + 1.0);
}