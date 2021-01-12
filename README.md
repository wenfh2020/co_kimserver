# co_kimserver

co_kimserver 是基于 `libco` 轻量级协程库的 tcp 高性能 C++ 多进程网络通信框架。

> 【注意】项目正在试验阶段，请谨慎使用！

---

## 1. 测试

单进程（libco 共享栈）服务本地压力测试：

400 个用户，每个用户发 10,000 个包，服务并发能力。

---

### 1.1. MacOS

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

### 1.2. Linux

并发：187,327 / s。

```shell
spend time: 16.0148
avg:        187327
send cnt:         3000000
callback cnt:     3000000
ok callback cnt:  3000000
err callback cnt: 0
```
