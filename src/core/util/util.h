#ifndef __KIM_UTIL_H__
#define __KIM_UTIL_H__

#include <google/protobuf/util/json_util.h>

#include <string>
#include <vector>
using google::protobuf::util::JsonStringToMessage;

template <typename T>
std::vector<T> diff_cmp(std::vector<T>& first, std::vector<T>& second) {
    std::vector<T> diff;
    std::sort(first.begin(), first.end(), std::less<T>());
    std::sort(second.begin(), second.end(), std::less<T>());
    std::set_difference(first.begin(), first.end(), second.begin(),
                        second.end(), std::inserter(diff, diff.begin()));
    return diff;
}

int str_to_int(const std::string& d);
void split_str(const std::string& s, std::vector<std::string>& vec, const std::string& seq = " ", bool trim_blank = true);
std::string format_addr(const std::string& host, int port);
std::string format_nodes_id(const std::string& host, int port, int index);
std::string format_str(const char* const fmt, ...);
std::string work_path();
std::string format_redis_cmds(const std::vector<std::string>& argv);
std::string md5(const std::string& data);

bool proto_to_json(const google::protobuf::Message& message, std::string& json);
bool json_file_to_proto(const std::string& file, google::protobuf::Message& message);
bool json_to_proto(const std::string& json, google::protobuf::Message& message);

#ifdef __cplusplus
extern "C" {
#endif

void daemonize();
bool adjust_files_limit(int& max_clients);
const char* to_lower(const char* s, int len);
long long mstime();     // millisecond
long long ustime();     // microseconds
double time_now();      // seconds (double)
double decimal_rand();  // [0, 1.0) double random.

#ifdef __cplusplus
}
#endif

#endif  //__KIM_UTIL_H__