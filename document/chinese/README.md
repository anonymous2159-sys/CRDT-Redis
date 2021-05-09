本项目基于 Redis (6.0.5) 进行拓展。这里包含四个文件夹：

* *redis-6.0.5* ：更改后的项目文件夹
* *redis_test* ：测试用配置文件和脚本，实验环境搭建
* *记录* ：阶段完成后的总结
* *bench*：实验命令生成与测试端代码

# 部署与测试

文件夹 *redis_test* 中包含测试用配置文件和脚本，测试会在本地启动 5 个 Redis 服务器，监听端口号为：6379, 6380, 6381, 6382, 6383，下面脚本参数从这几个端口号中选一个到多个

有 5 个脚本文件：

* server.sh [+参数] ：启动这五个服务器。有参数则启动参数指定服务器
* construct_replication.sh [+参数] ：将五个服务器建立 P2P 复制。有参数则为指定服务器建立复制
* client.sh <端口号> ：启动客户端链接指定端口号服务器
* shutdown.sh [+参数] ：关闭五个服务器。有参数则关闭指定服务器
* clean.sh [+参数] ：清除五个服务器的数据库数据文件和日志文件。有参数则清除指定数据库的文件

首先，编译并安装修改后的 Redis，进入 redis_test 文件夹：

    cd redis-6.0.5
    sudo make install
    cd ../redis_test

之后，启动 Redis 服务器并建立 P2P 复制

    ./server.sh
    ./construct_replication.sh

此时可以用 redis 客户端链接服务器

    ./client.sh <端口号>

测试完毕后关闭 5 个 Redis 客户端

    ./shutdown.sh

最后，可以清除 Redis 数据库数据文件以及日志文件

    ./clean.sh
    
# 阶段性进展

|	No.	|	名称		|	概要	|	
| ------------- | --------------------- | --------------------- |
| 1		| [P2P 拓展](./p2p.md)		|	完成了对 redis 的 P2P 复制拓展，建立起简单的 P2P 连接		| 
| 2		| [逻辑时钟实现](./vector_clock.md)	|	实现了逻辑时钟，有简单的操作以及命令，以及其持久化支持	| 
| 3		| [add-win RPQ 实现](./add-win%20RPQ.md)	|	依照算法设计，实现了 add-win RPQ (ORI RPQ)	| 
| 4 	| [remove-win RPQ 实现](./remove-win%20RPQ.md)	|	依照算法设计，实现了 remove-win RPQ	|