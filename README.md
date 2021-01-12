# co_kimserver

co_kimserver 是基于 `libco` 轻量级协程库的 tcp 高性能 C++ 多进程网络通信框架。

> 【注意】项目正在编写阶段，尚未完成，请谨慎使用！

---

## 1. 编译

co_kimserver 根目录，执行编译脚本。

```shell
 ./run.sh compile all
```

---

## 2. 运行

编程成功后，进入 bin 目录运行执行问题。

```shell
cd bin
./co_kimserver config.json
```

---

## 3. 测试

[压测源码](https://github.com/wenfh2020/co_kimserver/tree/main/src/test/test_tcp_pressure)。

单进程（libco 共享栈）服务本地压力测试：

400 个用户，每个用户发 10,000 个包，服务并发能力。

---

### 3.1. MacOS

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

### 3.2. Linux

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
