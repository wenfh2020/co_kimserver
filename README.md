# co_kimserver

> <font color=red>【注意】</font> 项目尚未完成，请谨慎使用！

---

## 1. 简述

`co_kimserver` 是高性能 TCP 网络通信框架。

* 多进程工作模式（manager/workers）。
* 基于腾讯开源的轻量级协程库 [libco](https://github.com/Tencent/libco)。
* 主要使用 C/C++11 语言开发。
* 支持 tcp 协议。
* 使用 protobuf 封装通信协议。
* 支持访问 mysql, redis (client: hiredis)。
* 通过 zookeeper 管理服务节点，支持分布式微服务部署。

---

## 2. 运行环境

项目支持 Linux 平台。源码依赖第三方库：

* mysqlclient
* protobuf3
* hiredis
* crypto++
* zookeeper_mt ([安装 zookeeper-client-c](https://wenfh2020.com/2020/10/17/zookeeper-c-client/))

>【注意】libco 不兼容 jemalloc / tcmalloc，出现死锁。

---

## 3. 架构

单节点多进程工作模式，支持多节点分布式部署。

---

### 3.1. 单节点

* manager 父进程：负责子进程管理调度，外部连接初始接入。
* worker 子进程：负责客户端详细连接逻辑。
* module 动态库：业务源码实现。(参考：[co_kimserver/src/modules/](https://github.com/wenfh2020/co_kimserver/tree/main/src/modules))

<div align=center><img src="doc/images/2021-02-19-07-25-03.png" width="85%"/></div>

---

### 3.2. 多节点

服务节点通过 `zookeeper` 发现其它节点。（下图是客户端与服务端多节点建立通信流程。）

<div align=center><img src="doc/images/2021-02-18-18-25-03.png"/></div>

---

## 4. 测试

4核 8G 虚拟机，本地压测：客户端与**单进程**服务网络通信，100w 条数据（10000 个用户，每个用户发 100 个包）。

* 压测命令。

    客户端（[测试源码](https://github.com/wenfh2020/co_kimserver/tree/main/src/test/test_tcp_pressure)），服务端（[测试源码](https://github.com/wenfh2020/co_kimserver/blob/main/src/modules/module_test/module_test.cpp)）。

```shell
# ./test_tcp_pressure [host] [port] [protocol(1001/1003/1005)] [users] [user_packets]
```

* 压测数据。

| 测试功能 | 数据量 | 并发        | 协议号 | 压测命令                                          |
| :------- | :----- | :---------- | :----- | :------------------------------------------------ |
| 普通协议 | 100w   | 200,000 / s | 1001   | ./test_tcp_pressure 127.0.0.1 3355 1001 10000 100 |

---

## 5. 服务配置

```shell
./bin/config.json
```

```shell
{
    "server_name": "kim-gate",              # 服务器名称。
    "worker_cnt": 1,                        # 子进程个数。（服务是多进程工作模式，类似 nginx。）
    "node_type": "gate",                    # 节点类型（gate/logic/...）。用户可以根据需要，自定义节点类型。
    "node_host": "127.0.0.1",               # 服务集群内部节点通信 host。
    "node_port": 3344,                      # 服务集群内部节点通信 端口。
    "gate_host": "127.0.0.1",               # 服务对外开放 host。（对外部客户端或者第三方服务。不对外服务可以删除该选项。）
    "gate_port": 3355,                      # 服务对外开放端口。（不对外服务可以删除该选项。）
    "gate_codec": "protobuf",               # 服务对外协议类型。目前暂时支持协议类型：protobuf。
    "keep_alive": 30,                       # 服务对外连接保活有效时间。
    "log_path": "kimserver.log",            # 日志文件。
    "log_level": "info",                    # 日志等级。(trace/debug/warn/info/notice/error/alert/crit)
    "max_clients": 10000,                   # 最大支持用户数量。
    "is_reuseport": false,                  # 支持 so_reuseport 选项。
    "modules": [                            # 业务功能插件，动态库数组。
        "module_test.so"
    ],
    "redis": {                              # redis 连接池配置，支持配置多个。
        "test": {                           # redis 配置节点，支持配置多个。
            "host": "127.0.0.1",            # redis 连接 host。
            "port": 6379,                   # redis 连接 port。
            "max_conn_cnt": 3               # redis 连接池最大连接数。
        }
    },
    "database": {                           # mysql 数据库连接池配置。
        "slowlog_log_slower_than": 300,     # mysql 执行 sql 命令，超过指定时间（单位：毫秒），打印慢日志。
        "nodes": {                          # mysql 连接池连接节点。
            "test": {                       # mysql 数据库配置节点，支持配置多个。
                "host": "127.0.0.1",        # mysql host。
                "port": 3306,               # mysql port。
                "user": "root",             # mysql 用户名。
                "password": "root123!@#",   # mysql 密码。
                "charset": "utf8mb4",       # mysql 字符集。
                "max_conn_cnt": 10          # mysql 连接池最大连接数。
            }
        }
    },
    "zookeeper": {                          # zookeeper 中心节点管理配置，用于节点发现，节点负载等功能。
        "servers": "127.0.0.1:2181",        # zookeeper 服务配置。
        "log_path": "zk.log",               # zookeeper-client-c 日志。
        "log_level": "debug",               # zookeeper-client-c 日志等级。(debug/warn/info/error)
        "root": "/kimserver",               # 节点发现根目录，保存了各个节点信息，每个节点启动需要往这个目录注册节点信息。
        "watch_node_type": [                # 当前节点关注其它节点类型（"node_type"），用于节点相互通信。
            "gate",                         # 接入节点类型。（用户可在配置自定义 "node_type"）
            "logic"                         # 逻辑节点类型。（用户可在配置自定义 "node_type"）
        ]
    }
}
```

---

## 6. 编译运行

```shell
cd ./co_kimserver
chmod +x run.sh
./run.sh all
```
