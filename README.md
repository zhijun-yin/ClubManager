# 社团管理系统 ClubManager

> 一个用 **C++17** 实现的社团管理命令行系统，把课程常见的几种数据结构
> （哈希表、AVL 平衡树、二叉堆、栈、队列、图、**倒排索引**）整合进一个真实可运行、
> 可测试的应用场景：成员 / 社团（含层级部门）/ 活动（含时间段与标签）/ 入社审批 /
> 好友关系，并支持活跃度排行、候补自动提拔、优先级审批、撤销 / 重做、公告广播、
> 批量导入、持久化等。

本项目是数据结构课程作业的第二个作品（第一个是 `RelationshipNetworkAnalyzer`，
关系网络分析工具）。二者都强调：**用合适的数据结构支撑真实功能，并可被测试验证**。

---

## 1. 功能一览

| 模块 | 功能 | 背后的数据结构 |
|------|------|----------------|
| 成员管理 | 注册（查重名）、删除（带保护）、活跃度增减、按姓名查、**打 / 删标签**、**批量导入 CSV** | 哈希表（姓名→id）、**倒排索引（标签→成员）** |
| 标签检索 | 单词查询、**多标签 AND / OR 查询**、列出全部标签 | **倒排索引 + 有序归并** |
| 社团管理 | 创建（重名校验）、**子部门（层级树）**、删除（有在册成员 / 子部门则拒绝）、入社 / 退社、**组织架构 DFS 打印** | 向量 + 哈希表 + **DFS** |
| 活动管理 | 创建（可带**结构化时间段**与**所需标签**）、报名、**满员进候补**、**取消 / 缩容自动提拔**、**时间段冲突检测** | 向量 + **二叉堆（大顶堆，按活跃度）** |
| 入社审批 | 提交申请、**按优先级 + 提交顺序处理**、通过 / 拒绝 | **优先队列（大顶堆）** |
| 好友关系 | 加好友 / 删好友、**二度人脉推荐**、**最短关系距离**、**最短社交路径** | **图（邻接表 + BFS）** |
| 好友驱动推荐 | 依据好友参加的活动为成员推荐活动 | 图 + 哈希计数 |
| 排行榜 | **Top-K 活跃度**、**个人排名**、**区间活跃人数**、**社团活跃度排行** | **AVL 平衡树（关键字 (score,id)）** |
| 公告广播 | 向社团发布公告，**按成员所在社团投递** | 向量 + 序号排序 |
| 撤销 / 重做 | 上一步回退 / 重做（加成员 / 加分会 / 审批 / 加好友 / 打标签 / 广播 / 导入等） | **双栈 + 命令模式** |
| 持久化 | 存档 / 读档（文本格式，含全部新字段） | 序列化 + 各索引重建 |

> **图形界面（多人共用）**：除命令行外，运行 `club gui` 启动一个本地 Web 图形界面，
> 团队成员在各自浏览器访问同一地址即可**实时协作修改同一套数据**（详见下文「图形界面」一节）。

### 设计要点

- **id = 向量下标**：成员 / 社团 / 活动都用 `vector` 存储，`id` 即下标，实现 O(1) 访问。
- **逻辑删除**：删除只是把 `active` 置 false，不移动向量，从而保持 `id == 下标` 不变量。
- **AVL 作有序索引**：以 `(score, id)` 为关键字，支持排名 / Top-K / 区间计数，均为 O(log n)。
- **候补用大顶堆**：`(活跃度, 成员id)` 入堆，满员时活跃度最高者优先；取消名额 / 缩容时 O(log n) 提拔。
  - 注意：堆内 `score` 是**报名时刻**的快照，提拔时按**当前**活跃度重新择优，保证增减活跃度后立即生效。
- **倒排索引**：标签 → 升序去重的成员 id 列表（有序数组），AND 查询用**双路归并求交**、OR 查询用 `std::merge` 求并。
- **优先队列审批**：待审申请按 `(优先级, 提交序号)` 大顶堆排序，高优先级先审、同优先级先提交者先审。
- **撤销 / 重做用双栈 + 命令模式**：每个修改操作同时记录逆操作与顺操作两个闭包，压入撤销栈；撤销弹栈执行逆操作并压入重做栈，重做反之。任何新修改会清空重做栈。
- **时间段冲突检测**：活动可带 `day` 与起止分钟数，报名前用区间重叠判断 `[s1,e1)∩[s2,e2)` 拒绝撞车。

---

## 2. 数据结构与复杂度

| 数据结构 | 实现位置 | 关键操作 | 复杂度 |
|----------|----------|----------|--------|
| 哈希表（链地址） | `hash_map.{h,cpp}` | put / get / contains / remove / rehash | 平均 O(1)，最坏 O(n) |
| AVL 平衡树 | `avl.{h,cpp}` | insert / remove / topK / rankOf / rangeCount | O(log n)，Top-K O(k+log n) |
| 二叉堆（大顶堆） | `heap.h` | push / pop / top / clear | O(log n) |
| 倒排索引 | `inverted_index.{h,cpp}` | add / remove / queryAnd / queryOr | 增删 O(log n)，AND/OR O(总词表长度) |
| 顺序栈 | `containers.h` | push / pop / top | O(1) |
| 循环队列 | `containers.h` | push / pop / front（自动扩容） | O(1) |
| 无向图 + BFS | `friend_graph.{h,cpp}` | addFriend / removeFriend / recommend / friendDistance / path | 增删 O(deg)，BFS O(V+E) |

> 所有平衡树 / 堆 / 哈希表 / 倒排索引 / 图均**自行实现**，未使用 `std::set` /
> `std::map` / `std::priority_queue` / `std::unordered_map`，以体现对底层结构的掌握。

---

## 3. 项目结构

```
ClubManager/
├── include/             # 头文件（数据结构声明 + 系统接口）
│   ├── containers.h     #   Stack / Queue
│   ├── hash_map.h       #   StringIntMap（链地址哈希表）
│   ├── avl.h            #   AVL 平衡树
│   ├── heap.h           #   BinaryHeap
│   ├── inverted_index.h #   InvertedIndex（倒排索引）
│   ├── friend_graph.h   #   FriendGraph（无向图 + BFS）
│   ├── models.h         #   Member / Club / Activity / Application / Announcement
│   └── system.h         #   ClubSystem（整合所有结构的核心类）
├── src/
│   ├── hash_map.cpp
│   ├── avl.cpp
│   ├── friend_graph.cpp
│   ├── inverted_index.cpp
│   ├── system.cpp       #   业务逻辑 + 撤销/重做 + 持久化
│   ├── main.cpp         #   命令行入口
│   ├── web_server.h     #   局域网 Web GUI 声明
│   └── web_server.cpp   #   HTTP 服务器 + 内嵌前端 + REST 路由
├── tests/
│   └── test_main.cpp    #   201 条断言的单元测试
├── examples/
│   └── sample_data.txt  #   示例存档
├── docs/
│   └── terminal_output.md  # 演示输出
├── Makefile
├── README.md
└── LICENSE
```

---

## 4. 编译与运行

需要 **g++（支持 C++17）**，例如 MinGW-w64 / MSYS2。

```bash
make              # 生成 build/club.exe
make demo         # 运行内置演示（一条命令展示全部功能）
make test         # 运行 201 条断言的单元测试
make gui          # 启动局域网多人共用 Web 图形界面(自动打开浏览器)
make clean        # 清理构建产物与运行时存档 club_data.txt
```

也可手动编译：

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude src/*.cpp -o club
./club demo
```

> Windows 下命令行参数与中文编码已通过 `CommandLineToArgvW` + UTF-8 转换处理。

---

## 5. 命令行用法

命令默认操作一个自动持久化的工作存档 `club_data.txt`（首次运行自动创建，
之后每次调用自动载入 / 保存），因此多条命令之间状态连续。

```text
用法: club <命令> [参数...]

  成员:   member add <姓名> <年级> [tag:标签...]
          member remove <id> | member score <id> <增减>
          member tag <id> <标签> | member untag <id> <标签>
          member tags <id>            # 查看成员标签
          member bytag <标签>         # 单词查询（倒排索引）
          member bytags all|any <t1> <t2>...   # 多标签 AND / OR
          member alltags              # 列出全部标签
          member import <文件>         # CSV 批量导入(姓名,年级,标签1;标签2)
          member list | member find <姓名>
  社团:   club create <名称> <分类> <成立> <简介>
          club sub <父id> <名称> <分类> <成立> <简介>   # 子部门
          club org                    # 组织架构(DFS)
          club remove <id> | club join <clubId> <memberId>
          club leave <clubId> <memberId> | club list
  活动:   activity add <clubId> <名称> <时间> <容量>
          activity addtimed <clubId> <名称> <容量> <日> <起> <止> [tag:标签...]
          activity remove <id>
          activity signup <memberId> <actId>     # 满员进候补；时间冲突则拒绝
          activity cancel <memberId> <actId>
          activity cap <actId> <新容量>          # 缩容降级 / 扩容提拔
          activity list | activity waitlist <actId>
  审批:   app submit <memberId> <clubId> [优先级]
          app process                    # 处理优先级最高者
          app review <appId> <approve|reject> | app pending
  好友:   friend add <a> <b> | friend remove <a> <b>
          friend recommend <id>          # 二度人脉推荐
          friend distance <a> <b>        # 最短关系距离
          friend path <a> <b>            # 最短社交路径(含端点)
          friend recact <id>             # 好友驱动活动推荐
  排行:   rank leaderboard [k] | rank member <id> | rank range <lo> <hi>
          rank clubleader [k]            # 社团活跃度排行
  公告:   announce broadcast <clubId> <senderId> <消息...>
          announce club <clubId>         # 某社团公告
          announce mine <memberId>       # 我能收到的公告
  其他:   undo | redo | save <path> | load <path> | demo | help
```

### 示例：载入示例存档并查询

```bash
./build/club.exe load examples/sample_data.txt   # 导入示例存档
./build/club.exe member list                     # 列出成员
./build/club.exe rank leaderboard 3              # Top-3 活跃度
./build/club.exe member bytag Python             # 查会 Python 的成员
./build/club.exe member bytags all Python 算法    # 同时会 Python 与算法
./build/club.exe club org                        # 组织架构
./build/club.exe activity waitlist 0             # 活动候补名单
./build/club.exe app pending                     # 待审申请数
./build/club.exe friend path 0 3                 # 最短社交路径
```

> **提示**：`undo` / `redo` 作用于**当前会话内**的操作历史。撤销 / 重做栈保存在内存中，
> 跨进程调用不延续（闭包无法序列化，这是命令模式撤销的固有特性）。单次会话中连续操作
> （如 `demo`）时撤销 / 重做可完整工作；跨 CLI 调用时历史会重置。

### 图形界面（多人共用 Web GUI）

不想敲命令时，运行 `club gui` 会启动一个**本地 Web 服务器**，浏览器打开
`http://127.0.0.1:8090` 即进入图形界面。把地址换成**运行机器的局域网 IP**，
同一局域网内的同事就能在各自浏览器里访问**同一份数据**、实时协作修改。

- **多人共用**：服务器持有唯一一份内存数据，所有人的操作即时作用于这份数据，
  并自动写入 `club_data.txt`（无需手动保存），一人改、其他人刷新即见。
- **简单身份选择**：顶栏下拉选择"我是谁"（成员），报名 / 取消 / 以本人身份发公告
  等操作即作用于该成员；无密码与角色，适合团队内部可信协作。
- **功能覆盖**：成员 / 社团 / 活动 / 审批 / 好友 / 排行 / 公告七个标签页，
  覆盖命令行全部功能，每个操作都有 toast 反馈；顶栏可撤销 / 重做 / 保存。
- **首次放行端口**：Windows 首次运行会弹窗请求放行 8090 端口（也可手动以管理员运行
  `netsh advfirewall firewall add rule name=ClubManager dir=in action=allow protocol=TCP localport=8090`）；
  放行后局域网内其他机器才能访问。
- **跨平台**：Windows 用 Winsock、Linux 用 POSIX sockets（`#ifdef _WIN32` 隔离），
  便于后续部署到云服务器（如 Oracle Cloud 常开免费层）对外公开访问。

---

## 6. 持久化格式

存档为纯文本，`|` 分隔字段、空行分段，便于查看与调试：

```text
# ClubManager save
clock|10
seq|m4|c1|a1|p1|n0
members
M|0|Alice|2022|20|1|1|Python;算法
END
clubs
C|0|编程社|学术科技|2020-09|热爱编程|1|0 1|-1|1
END
activities
A|0|0|算法周赛|周六19:00|2|1|0|2 3|Python;算法|5|1140|1200
END
applications
P|0|2|0|5|0|0
END
announcements
N|0|0|0|今晚周赛|6
END
friends
F|0|1
END
```

- 字段含义见 `src/system.cpp` 的 `save()` / `load()`。
- 名单（社团成员、活动正取名额 / 候补）用空格分隔的 id 列表；标签用 `;` 分隔。
- 成员行末段为标签，社团行末段为 `parentClub` 与子部门 id 列表，活动行包含正取名额、
  候补、所需标签与结构化时间段（`day` / `startMin` / `endMin`）。
- 读档时重建所有索引（哈希表、AVL、倒排索引、图、优先队列）。

---

## 7. 测试

`make test` 运行 **201 条断言**，覆盖：

- **哈希表**：增删查、更新、rehash 扩容。
- **AVL**：插入触发旋转、同分不同 id、Top-K、排名、区间计数、中序有序、删除后平衡。
- **二叉堆**：降序弹出、pair 比较。
- **栈 / 队列**：栈 LIFO、队列 FIFO 与扩容、环绕。
- **倒排索引**：加词、AND / OR 归并、删除、空表回收。
- **图**：无向边、BFS 距离、二度推荐、**最短路径回溯**、删边。
- **系统层**：成员查重、删除保护、活跃度排行、社团删除保护、组织架构 DFS、
  候补入堆与提拔、**按当前活跃度重新择优**、**时间段冲突检测**、容量缩容 / 扩容、
  申请优先级排序、审批通过 / 拒绝、打 / 删标签、**撤销 + 重做双栈**、公告广播、
  好友驱动活动推荐、批量导入、**保存 / 载入往返一致性（含正取名额持久化）**。

```text
==================================
ALL TESTS PASSED (201 checks)
```

---

## 8. 已知限制

- 撤销 / 重做历史为会话内有效，不跨进程持久化（见上文提示）。
- Web GUI 为**局域网多人共用**设计：数据由运行 `club gui` 的单一服务器持有并自动存档，
  身份仅为下拉选择、无鉴权，适合可信局域网 / 团队内部协作，不应直接暴露到公网。
- 存档字段以 `|` 分隔，名称 / 简介 / 公告等文本字段不应包含 `|`（CLI 层未做转义）。
- 逻辑删除会在向量中留下空位（`active=false`），换取 `id==下标` 的 O(1) 访问；
  这是空间换时间的有意取舍。
- 时间段冲突检测仅在活动提供结构化 `day` / 起止时间时生效；纯文本时间（如"周六 19:00"）
  不参与冲突判断。

---

## 许可证

MIT License，见 [LICENSE](LICENSE)。
