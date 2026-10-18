#ifndef WEB_SERVER_H
#define WEB_SERVER_H

class ClubSystem;

// 启动本地 Web GUI：在 127.0.0.1 上起一个轻量 HTTP 服务器，把 sys 暴露为 REST API，
// 浏览器打开 http://127.0.0.1:8090 即图形界面（含中英法韩四语切换）。阻塞运行，Ctrl+C 退出。
void startGui(ClubSystem& sys);

#endif  // WEB_SERVER_H
