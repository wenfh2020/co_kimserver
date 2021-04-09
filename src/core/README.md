# 源码文件

```shell
+ codec                    # 协议封装 tcp (protobuf) / http。
  - codec_http.h           # http 协议包。（protobuf）
  - codec_http.cpp         # ~
  - codec_proto.h          # tcp 协议包。
  - codec_proto.cpp        # ~
  - codec.h                # 协议基础接口。
  - codec.cpp              # ~
+ libco                    # 腾讯开源的 libco 协程库。（详细请参考 https://github.com/Tencent/libco）
+ mysql                    # mysql 连接池。
  - mysql_conn.h           # mysql 客户端连接。
  - mysql_conn.cpp         # ~
  - mysql_mgr.h            # mysql 连接管理。
  - mysql_mgr.cpp          # ~
  - mysql_result.h         # mysql 查询数据结果集解析。
  - mysql_result.cpp       # ~
+ net                      # 网络通信常用接口。
  - anet.h                 # 网络异步通信功能接口。
  - anet.cpp               # ~
  - channel.h              # 管道通信，主要是父子进程通信，socket 透传，数据传输。
  - channel.cpp            # ~
+ protobuf                 # protobuf 协议以及基础数据结构。
  + proto                  # protobuf 协议。
    - http.proto           # http 协议。
    - msg.proto            # tcp 协议。
  + sys                    # 系统使用的基础数据。
    - nodes.proto          # 节点信息，zookeeper 节点信息，通过 protobuf 封装便于操作，也方便于 json 数据相互转化。
    - payload.proto        # 节点的父子进程负载信息。
  - gen_proto.sh           # protobuf 脚本，将 proto 文件，生成 c++ 源码。
+ redis                    # redis 连接池。
  - redis_mgr.h            # reddis 连接管理，封装了 hiredis。
  - redis_mgr.cpp          # ~
+ util                     # 常用工具类。
  + http                   # http 协议解析。
    - http_parser.h        # 异步 http 协议解析。（详细参考开源：https://github.com/nodejs/http-parser）
    - http_parser.cpp      # ~
  + json                   # 轻量级 json 解析。
    - cJson.h              # 基于 c 语言的 json 数据结构解析。（https://github.com/DaveGamble/cJSON）
    - cJson.cpp            # ~
    - CJsonObject.hpp      # 在 cJson.h 基础上通过 C++ 进行封装。
    - CJsonObject.cpp      # ~
    - hash.h               # 字符串生成哈希值接口。  
    - hash.cpp             # ~
    - log.h                # 日志。
    - log.cpp              # ~
    - reference.h          # 索引类，用于保护对象，避免使用过程中，被释放。
    - set_proc_title.h     # 设置进程名称接口。
    - set_proc_title.cpp   # ~
    - so.h                 # 动态库基础类。
    - socket_buffer.h      # socket 读写数据内存缓冲区。
    - socket_buffer.cpp    # ~
    - util.h               # 基础工具接口。
    - util.cpp             # ~
+ zookeeper                # zookeeper client 封装，节点管理。
  - zk_bio.h               # 多线程处理 zookeeper-client-c 异步命令。
  - zk_bio.cpp             # 
  - zk_task.h              # zookeeper 通信命令。
  - zk_task.cpp            # ~
  - zk.h                   # 封装 zookeeper-client-c 中间件。(https://wenfh2020.com/2020/10/17/zookeeper-c-client/)
  - zk.cpp                 # ~
- base.h                   # 封装了基础类数据。对象 id，日志对象，网络对象。
- connection.h             # 连接对象。封装 socket 的网络数据读写。
- connection.cpp           # ~
- coroutines.h             # 协程池，主要是客户端接入的协程管理，但不包括 mysql, redis 等少量的协程管理。
- coroutines.cpp           # ~
- error.h                  # 错误码。
- Makefile                 # 编译 libco 和 core 源码 makefile 文件。
- Makefile.core            # 编译 core 源码 makefile 文件。
- manager.h                # 主进程，主要负责子进程管理，网络初始接入。
- manager.cpp              # ~
- module_mgr.h             # 业务动态库管理。业务源码被封装成多个 so 动态库载入进程。
- module_mgr.cpp           # ~
- module.h                 # 业务动态库模块。具体使用参考 co_kimserver/src/modules 测试源码。
- module.cpp               # ~
- net.h                    # 网络通信接口，作为基础对象。
- network.h                # 网络通信逻辑实现。继承 net.h。
- network.cpp              # ~
- node_connection.h        # 集群，节点间通信源码。 
- node_connection.cpp      # ~
- nodes.h                  # 节点数据，封装了一致性哈希算法。
- nodes.cpp                # ~
- protocol.h               # 协议号。
- request.h                # 网络请求封装。
- request.cpp              # ~
- server.cpp               # 服务入口。
- server.h                 # 服务公共变量宏定义公共头文件。
- session.h                # 会话，数据缓存对象，有定时功能。
- session.cpp              # ~
- sys_cmd.h                # 系统内部通信协议处理。
- sys_cmd.cpp              # ~
- timer.h                  # 时钟，通过协程实现。
- timers.h                 # 多定时器，通过 map 实现。
- timers.cpp               # ~
- worker_data_mgr.h        # 子进程数据，在主进程中使用。
- worker_data_mgr.cpp      # ~
- worker.h                 # 子进程逻辑实现，负责详细的网络通信逻辑。
- worker.cpp               # ~
- zk_client.h              # zookeeper 客户端封装，多线程实现，负责节点信息管理。
- zk_client.cpp            # ~
```
