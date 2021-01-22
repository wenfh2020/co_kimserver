# co_kimserver

## 1. 简述

`co_kimserver` 是高性能的 TCP 网络通信框架。

* 多进程框架（manager/workers）。
* 基于腾讯开源的轻量级协程库 [libco](https://github.com/Tencent/libco)。
* 主要使用 C/C++11 语言开发。
* 支持 tcp 协议。
* 使用 protobuf 封装协议包。

> <font color=red>【注意】</font> 项目尚未完成，请谨慎使用！

---

## 2. 运行环境

项目支持 Linux / MacOS 平台。

>【注意】MacOS 平台 Libco 不能成功 hook mysqlclient 等第三方库的阻塞接口，所以 MacOS 平台的某些同步功能不能成功被改造成异步，虽然这样，服务仍然能正常工作。

源码编译前需要先安装依赖的第三方库：

* mysqlclient
* protobuf
* hiredis
* cryptopp
* zookeeper_mt ([安装 zookeeper-client-c](https://wenfh2020.com/2020/10/17/zookeeper-c-client/))

>【注意】Linux 环境，Libco 不支持与 jemalloc 同时使用，jemalloc 容易出现死锁。

---

## 3. 编译

到 co_kimserver 根目录，执行编译脚本。

```shell
 ./run.sh compile all
```

---

## 4. 运行

编译成功后，进入 bin 目录运行启动服务。

```shell
cd bin
./co_kimserver config.json
```

---

## 5. 测试

[压测源码](https://github.com/wenfh2020/co_kimserver/tree/main/src/test/test_tcp_pressure)。

单进程（libco 共享栈）服务本地压力测试：

400 个用户，每个用户发 10,000 个包，服务并发能力。

---

### 5.1. MacOS

并发：125,006 / s。

```shell
# ./test_tcp_pressure 127.0.0.1 3355 400 10000
spend time: 31.9985
avg:        125006
send cnt:         4000000
callback cnt:     4000000
ok callback cnt:  4000000
err callback cnt: 0
```

---

### 5.2. Linux

并发：184,838 / s。

```shell
# ./test_tcp_pressure 127.0.0.1 3355 400 10000
spend time: 21.6406
avg:        184838
send cnt:         4000000
callback cnt:     4000000
ok callback cnt:  4000000
err callback cnt: 0
```
