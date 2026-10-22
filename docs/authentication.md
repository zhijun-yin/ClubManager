# 登录与权限

## 首次使用

在项目目录先构建，再初始化管理员。密码通过隐藏输入读取，不会放进命令行参数或日志。

```powershell
.\run-wsl.ps1 -Action init -Username admin
.\run-wsl.ps1
```

脚本自动完成 WSL 构建，服务地址为 `http://127.0.0.1:8090/`。原生 Windows 兼容入口为 `build.ps1` 和 `build/club.exe auth init admin`，当前机器使用 WSL 以保持 Windows 安全设置不变。

密码要求为 10–128 字节；推荐使用足够长的独立密码。账号为 3–32 位英文字母、数字、点、下划线或横线，区分大小写。已有账号文件时不能再次初始化；未初始化时网页只显示本机初始化说明，不允许远程抢注管理员。

管理员登录后，在“成员”页建成员，再在“账号权限”页创建账号、选择“普通成员”、绑定该成员。每个有效成员绑定只能分配一个账号。管理员可以不绑定成员。停用账号仍保留历史记录，恢复启用后可以重新登录。

忘记密码：由另一位管理员在账号页重置；若唯一管理员忘记密码，在服务器本机停止服务后执行 `./run-wsl.ps1 -Action reset -Username admin`（Linux 直接运行时为 `club auth reset admin`），按提示设置新密码，再启动。CLI 拥有本机维护权限，不能把服务器终端或数据文件访问权交给普通成员。

## 角色矩阵

| 能力 | 管理员 | 普通成员 |
|---|---|---|
| 活动与社团信息 | 全部管理 | 浏览公开信息，成员名单只含本人 |
| 报名、取消、签到 | 可代操作所选成员 | 仅本人，身份来自登录绑定 |
| 入社申请 | 提交、调整优先级、审核、撤回代操作成员申请 | 提交本人普通优先级申请、撤回本人待审申请 |
| 申请列表与待审数 | 全部 | 仅本人 |
| 个人资料、日程 | 所有成员 | 仅本人 |
| 公告 | 发布及查询 | 仅查看自己所属社团的公告 |
| 成员、标签、积分、容量、好友 | 管理 | 禁止 |
| CSV、存档、全局撤销/重做 | 管理 | 禁止 |
| 创建账号、绑定、改角色、启停、重置密码 | 允许；不能停用/降级最后一位有效管理员 | 禁止 |
| 修改自己密码、退出 | 允许 | 允许 |

角色、账号状态、成员绑定在每个请求中重新验证。成员接口采用明确允许列表，新增加的管理接口不会自动开放给普通成员。对伪造其他 `memberId`、非零申请优先级、他人申请 ID 或管理路径的请求返回 HTTP 403。重复表单参数返回 HTTP 400。未登录、会话失效返回 HTTP 401。

业务条件仍然独立有效：报名资格、容量、时间冲突、不可重复签到、签到后不可取消、结束后不可修改，均按原有规则执行。

## 凭据与会话

- Windows BCrypt / 非 Windows OpenSSL 执行 PBKDF2-HMAC-SHA256，600,000 次迭代，随机 16 字节盐、32 字节哈希。账号文件不存明文密码。
- 会话令牌与 CSRF 令牌分别由系统安全随机源生成 32 字节；会话保存在服务器内存，固定 8 小时有效，重启后需要重新登录。
- 同一账号再次登录会使旧会话失效；停用、改角色、改绑定、重置或修改密码也会使旧会话立即失效。密码修改需要验证当前密码。
- Cookie 设置 HttpOnly、SameSite=Strict、Path=/。启用 `CLUB_SECURE_COOKIE=1` 后添加 Secure，需由外部 HTTPS 反向代理提供 TLS，且转发外部 Host；不能在普通 HTTP 局域网地址直接使用此设置。
- 所有已登录写请求都需要 `X-CSRF-Token`；同时检查浏览器 Origin。会话令牌不写入 localStorage；接口响应禁止缓存。
- 同一连接来源 IP 在 60 秒内失败 5 次后暂时拒绝登录。采用 socket 实际来源，不信任任意 `X-Forwarded-For`。共享出口或反向代理后的用户可能共享该限制，教学服务暂未实现受信代理 IP 配置。

本项目是本机/小规模社团教学应用，尚未包含 MFA、邮件找回、强制首次改密、持久化安全审计、分布式会话或生产负载防护。内置 HTTP 不加密网络流量；密码哈希不能替代 HTTPS。

## 存档与恢复

`club_accounts.txt` 存账号及密码哈希，`club_data.txt` 存业务实体及随机成员绑定标识。两者位于进程工作目录，均已忽略 Git；备份应一起保存，并限制目录访问权限。业务 CSV 不含账号凭据，`save`/`load` 不会覆盖账号库。

绑定由“成员 ID + 稳定随机标识”共同确认。这样导入另一份数据时，即使存在相同成员 ID，也不会自动把旧账号授权给新人。v1/v2 没有绑定标识，管理员应在导入后逐一核实并重新绑定账号。普通业务撤销/重做会保留原成员的绑定标识。

账号文件保存采用临时文件写完再替换；损坏或缺失结束标记会使启动失败，保留原文件供恢复，不会静默重置管理员。数据与账号不是跨文件数据库事务，停服备份更容易保持一致；不要同时运行多个进程写同一个工作目录。

## 接口

请求体使用 `application/x-www-form-urlencoded`。JSON 结构沿用 `{ok,data}` / `{ok:false,error}`。

| 接口 | 用途 |
|---|---|
| `GET /api/auth/status` | 初始化状态；已登录时返回本人账号信息及 CSRF 令牌 |
| `POST /api/auth/login` | `username,password`，成功设置会话 Cookie，返回 `user,csrf` |
| `POST /api/auth/logout` | 注销当前会话 |
| `POST /api/auth/password` | `currentPassword,password`，修改本人密码并注销旧会话 |
| `GET /api/accounts` | 管理员查询账号与绑定是否有效，不返回盐/哈希/绑定标识 |
| `POST /api/accounts` | 管理员创建：`username,password,role,memberId` |
| `POST /api/accounts/{username}` | 管理员编辑：`role,enabled,memberId`；省略绑定表示不绑定，仅管理员允许 |
| `POST /api/accounts/{username}/password` | 管理员重置：`password` |

`role` 只允许 `admin` / `member`；`enabled` 只允许 `true` / `false`。普通成员允许的业务 API 与角色矩阵对应，服务器从会话取本人 ID；可省略 `memberId`，如果提交必须与本人一致。

## 设计参考

平台密码学 API 使用 [Microsoft BCryptDeriveKeyPBKDF2](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekeypbkdf2) 和 [OpenSSL PKCS5_PBKDF2_HMAC](https://docs.openssl.org/3.0/man3/PKCS5_PBKDF2_HMAC/)。迭代参数参考 [OWASP Password Storage Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html)。项目没有自行编写密码散列算法。
