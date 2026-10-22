// web_server.cpp
// 局域网多人共用 Web 界面：把 ClubSystem 暴露为本地 HTTP 服务器（REST API），
// 浏览器打开即图形界面。跨平台：Windows 用 Winsock，Linux 用 POSIX sockets（#ifdef 切换）。
// Cookie 会话认证；管理员与普通成员权限在服务端分别处理。
// 无四语切换（按需求聚焦"多人共用"）。

#include "web_server.h"
#include "system.h"
#include "auth.h"

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <functional>
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <csignal>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <shellapi.h>
  typedef SOCKET sock_t;
  #define SOCK_BAD_ INVALID_SOCKET
  #define SOCK_CLOSE_ closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <netdb.h>
  typedef int sock_t;
  #define SOCK_BAD_ (-1)
  #define SOCK_CLOSE_ close
#endif

static std::mutex g_mutex;                 // 保护 sys，串行化并发请求
static int serverPort() {
    const char* env = std::getenv("CLUB_PORT");
    if (!env) return 8090;
    size_t used = 0; int port = std::stoi(env, &used);
    if (used != std::strlen(env) || port < 1 || port > 65535) throw std::invalid_argument("invalid CLUB_PORT");
    return port;
}
static const char* SAVE_FILE = "club_data.txt";

// ---------------------------------------------------------------------------
// 嵌入的前端单页（中文 UI，无四语切换）。原生 JS，无外部依赖。
// 管理员可以选择代操作成员；普通成员身份固定为账号绑定的成员。
// ---------------------------------------------------------------------------
static const std::string kHtml = R"CLUBGUI(<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>社团管理系统 · 多人共用</title>
<style>
*{box-sizing:border-box}
body{margin:0;font-family:"Microsoft YaHei","Segoe UI",Arial,sans-serif;background:#f4f6fb;color:#1f2937}
header{display:flex;align-items:center;gap:12px;padding:10px 18px;background:#1e3a8a;color:#fff;flex-wrap:wrap}
header .brand{font-size:17px;font-weight:700}
header .grow{flex:1}
header .lab{color:#cbd5e1;font-size:13px}
select,header button{font-size:14px;padding:6px 10px;border-radius:6px;border:1px solid #cbd5e1}
header button{background:#2563eb;color:#fff;border:none;cursor:pointer}
header button:hover{background:#1d4ed8}
header .me{background:#0ea5e9}
.layout{display:flex;min-height:calc(100vh - 52px)}
nav{width:140px;background:#fff;border-right:1px solid #e5e7eb;padding:10px 0}
nav button{display:block;width:100%;text-align:left;padding:11px 18px;background:none;border:none;font-size:15px;cursor:pointer;color:#374151}
nav button:hover{background:#f1f5f9}
nav button.active{background:#dbeafe;color:#1e3a8a;font-weight:600;border-left:3px solid #2563eb}
main{flex:1;padding:18px 22px}
h2{margin:0 0 12px}
table{width:100%;border-collapse:collapse;background:#fff;border-radius:8px;overflow:hidden;box-shadow:0 1px 3px rgba(0,0,0,.08);margin-bottom:14px}
th,td{padding:8px 10px;text-align:left;border-bottom:1px solid #eef2f7;font-size:14px}
th{background:#f8fafc;color:#475569;font-weight:600}
tr:hover td{background:#f8fafc}
.card{background:#fff;border-radius:8px;padding:14px;box-shadow:0 1px 3px rgba(0,0,0,.08);margin-bottom:14px}
.card h3{margin:0 0 10px;font-size:15px;color:#334155}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
input,select{padding:7px 9px;border:1px solid #cbd5e1;border-radius:6px;font-size:14px}
button.btn{padding:7px 12px;border:none;border-radius:6px;background:#2563eb;color:#fff;cursor:pointer;font-size:14px}
button.btn:hover{background:#1d4ed8}
button.gray{background:#64748b}button.gray:hover{background:#475569}
button.danger{background:#dc2626}button.danger:hover{background:#b91c1c}
button.sm{padding:4px 8px;font-size:13px}
.tag{display:inline-block;background:#e0e7ff;color:#3730a3;padding:2px 8px;border-radius:999px;font-size:12px;margin:1px 2px}
#toast{position:fixed;top:18px;right:18px;padding:11px 18px;border-radius:8px;color:#fff;font-size:14px;opacity:0;transition:opacity .2s;pointer-events:none;z-index:99}
#toast.show{opacity:1}#toast.ok{background:#16a34a}#toast.err{background:#dc2626}
.muted{color:#94a3b8;font-size:13px}
pre{white-space:pre-wrap}
main{min-width:0;overflow-x:auto;max-width:1500px}nav{flex-shrink:0}
button:disabled{opacity:.4;cursor:not-allowed!important}button:focus-visible,a:focus-visible,summary:focus-visible{outline:3px solid #60a5fa;outline-offset:3px}
.download{display:inline-block;color:#1d4ed8;padding:6px;text-decoration:none;font-size:14px}
.metrics{display:grid;grid-template-columns:repeat(auto-fit,minmax(155px,1fr));gap:14px;margin-bottom:20px}
.metric{padding:22px;background:#fff;border:1px solid #e2e8f0;border-radius:12px}.metric strong{display:block;font-size:32px;color:#1e3a8a;margin-top:8px}.metric span{color:#64748b;font-size:14px}
.hero{padding:24px;border-radius:12px;background:#e8efff;margin-bottom:20px}.hero p{color:#475569;margin-bottom:0;line-height:1.8}
.two-col{display:grid;grid-template-columns:1fr 1fr;gap:16px}.empty{padding:24px;color:#64748b;text-align:center}
progress{width:100%;height:12px;accent-color:#2563eb}summary{cursor:pointer;font-weight:600}dialog{border:0;border-radius:14px;padding:28px;max-width:600px;width:90%;box-shadow:0 20px 80px #0004}dialog::backdrop{background:#0f172a77}td{overflow-wrap:anywhere}h2{font-size:23px}
[hidden]{display:none!important}.login{max-width:450px;margin:8vh auto;padding:30px}.login label{display:block;margin:18px 0 7px}.login input{width:100%}.login .btn{width:100%;margin-top:22px;padding:12px}.login p{line-height:1.8;color:#64748b}.account-actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:16px}label.field{display:flex;flex-direction:column;gap:6px;color:#475569;font-size:13px}.session-tools{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.session-user{font-size:13px;color:#dbeafe}
@media(max-width:760px){.layout{display:block}nav{display:flex;overflow-x:auto;width:100%;padding:4px;border-right:0}nav button{width:auto;white-space:nowrap;padding:12px}main{padding:16px}.two-col{grid-template-columns:1fr}header{gap:8px}table{min-width:640px}.hero{padding:18px}}
</style>
</head>
<body>
<header>
  <span class="brand">🎭 社团管理系统</span>
  <span class="lab">社团日常 · 有序协作</span>
  <span class="grow"></span>
  <div id="session_tools" class="session-tools" hidden><span id="session_user" class="session-user"></span>
  <span id="identity_label" class="lab">代操作成员：</span>
  <select id="me"></select>
  <button id="undo">撤销</button>
  <button id="redo">重做</button>
  <button id="refresh">刷新数据</button><button id="save">保存</button>
  <button id="password">修改密码</button><button id="logout">退出登录</button></div>
</header>
<div class="layout">
  <nav id="nav" hidden></nav>
  <main id="main"></main>
</div>
<div id="toast"></div>
<script>
const $=s=>document.querySelector(s), main=$('#main'), nav=$('#nav'), meSel=$('#me');
let ME=0, members=[], cur='dashboard', renderVersion=0, USER=null, CSRF='';
const TABS=[{k:'dashboard',n:'总览'},{k:'schedule',n:'我的日程'},{k:'members',n:'成员'},{k:'clubs',n:'社团'},{k:'activities',n:'活动'},{k:'apps',n:'审批'},{k:'friends',n:'好友'},{k:'rank',n:'排行'},{k:'announce',n:'公告'},{k:'accounts',n:'账号权限'}];
const esc=s=>String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
async function api(method,path,body){
  const o={method,headers:{},credentials:'same-origin'};
  if(method!=='GET'&&CSRF)o.headers['X-CSRF-Token']=CSRF;
  if(body!=null){o.headers['Content-Type']='application/x-www-form-urlencoded';o.body=toForm(body);}
  try {
    const response=await fetch(path,o), r=await response.json();
    if(response.status===401&&path!=='/api/auth/login'){showLogin(true);toast('登录已失效，请重新登录',false);throw new Error('session-expired');}
    if(!r.ok) toast(r.error||'请求失败',false);
    return {...(r.data||{}),...r};
  } catch(error) {if(error.message!=='session-expired')toast('连接失败，请检查服务器是否仍在运行',false);throw error;}
}
function toForm(o){return Object.keys(o).filter(k=>o[k]!=null&&o[k]!=='').map(k=>encodeURIComponent(k)+'='+encodeURIComponent(o[k])).join('&');}
function toast(msg,ok=true){const t=$('#toast');t.textContent=msg;t.className='show '+(ok?'ok':'err');clearTimeout(t._h);t._h=setTimeout(()=>t.className='',2200);}
function buildPicker(){const act=members.filter(x=>x.active);meSel.innerHTML=act.map(m=>`<option value="${m.id}">${esc(m.name)} (#${m.id})</option>`).join('');if(USER?.role==='member')ME=USER.memberId;else if(!act.some(m=>m.id===ME))ME=act.length?act[0].id:0;meSel.value=ME;meSel.disabled=USER?.role!=='admin';meSel.onchange=()=>{if(USER?.role==='admin'){ME=parseInt(meSel.value);render();}};}
async function refreshMe(){const d=await api('GET','/api/members');members=d.members||[];buildPicker();}
function buildNav(){const tabs=USER?.role==='admin'?TABS:[{k:'schedule',n:'我的日程'},{k:'activities',n:'活动报名'},{k:'clubs',n:'社团入社'},{k:'apps',n:'我的申请'},{k:'announce',n:'我的公告'}];nav.innerHTML=tabs.map(t=>`<button data-tab="${t.k}">${t.n}</button>`).join('');nav.querySelectorAll('button').forEach(b=>b.onclick=()=>{cur=b.dataset.tab;render();});}
async function render(){if(!USER)return;const version=++renderVersion;await refreshMe();if(version!==renderVersion||!USER)return;nav.querySelectorAll('button').forEach(b=>b.classList.toggle('active',b.dataset.tab===cur));const views=USER.role==='admin'?{dashboard:renderDashboard,schedule:renderSchedule,members:renderMembers,clubs:renderClubs,activities:renderActivities,apps:renderApps,friends:renderFriends,rank:renderRank,announce:renderAnnounce,accounts:renderAccounts}:{schedule:renderSchedule,activities:renderMemberActivities,clubs:renderMemberClubs,apps:renderMemberApps,announce:renderMemberAnnouncements};(views[cur]||renderSchedule)().catch(()=>{});if(USER.role==='admin')updateHistory();}

function showLogin(initialized){USER=null;CSRF='';members=[];ME=0;++renderVersion;document.querySelectorAll('dialog').forEach(d=>d.remove());$('#session_tools').hidden=true;nav.hidden=true;nav.innerHTML='';main.innerHTML=`<section class="card login"><span class="tag">CLUB MANAGER</span><h2 style="margin-top:18px">欢迎回到社团</h2><p>登录后管理你的活动与入社申请。</p>${initialized?'<form id="login_form"><label for="login_user">账号</label><input id="login_user" name="username" autocomplete="username" required maxlength="32" autofocus><label for="login_pass">密码</label><input id="login_pass" name="password" type="password" autocomplete="current-password" required maxlength="128"><button class="btn" type="submit">登录</button><p id="login_error" role="alert"></p></form><p class="muted">还没有账号？请联系社团管理员创建并绑定成员。忘记密码时请联系管理员重置。</p>':'<h3>首次使用</h3><p>请在服务器本机停止服务，运行以下命令设置管理员账号和密码，然后重新启动：</p><pre>club auth init admin</pre><p>系统没有默认账号或默认密码。</p>'}</section>`;if(initialized)$('#login_form').onsubmit=async e=>{e.preventDefault();const button=e.currentTarget.querySelector('button');button.disabled=true;try{const r=await api('POST','/api/auth/login',{username:$('#login_user').value,password:$('#login_pass').value});if(r.ok){$('#login_pass').value='';await enterSession(r);}else $('#login_error').textContent=r.error;}finally{button.disabled=false;}};}
async function enterSession(r){USER=r.user;CSRF=r.csrf;ME=USER.memberId>=0?USER.memberId:0;cur=USER.role==='admin'?'dashboard':'schedule';$('#session_user').textContent=USER.username+' · '+(USER.role==='admin'?'管理员':'普通成员');$('#identity_label').textContent=USER.role==='admin'?'代操作成员：':'当前成员：';['undo','redo','save'].forEach(id=>$('#'+id).hidden=USER.role!=='admin');$('#session_tools').hidden=false;nav.hidden=false;buildNav();await render();}
async function boot(){const r=await api('GET','/api/auth/status');if(r.authenticated)await enterSession(r);else showLogin(r.initialized);}
function passwordDialog(username){const self=username==null, box=document.createElement('dialog');box.innerHTML=`<h2>${self?'修改我的密码':'重置 '+esc(username)+' 的密码'}</h2><p class="muted">密码需 10–128 字节。保存后该账号的旧会话立即失效。</p><form>${self?'<label class="field">当前密码<input name="currentPassword" type="password" autocomplete="current-password" required></label>':''}<label class="field">新密码<input name="password" type="password" autocomplete="new-password" minlength="10" maxlength="128" required></label><label class="field">确认新密码<input name="confirm" type="password" autocomplete="new-password" required></label><div class="account-actions"><button class="btn" type="submit">保存密码</button><button class="btn gray" type="button">取消</button></div><p role="alert"></p></form>`;document.body.append(box);box.querySelector('[type=button]').onclick=()=>box.close();box.onclose=()=>box.remove();box.querySelector('form').onsubmit=async e=>{e.preventDefault();const form=e.currentTarget,values=Object.fromEntries(new FormData(form));if(values.password!==values.confirm){box.querySelector('[role=alert]').textContent='两次密码不一致';return;}const button=form.querySelector('[type=submit]');button.disabled=true;try{const r=await api('POST',self?'/api/auth/password':`/api/accounts/${encodeURIComponent(username)}/password`,values);if(r.ok){box.close();toast('密码已更新，请使用新密码登录');if(self||username===USER?.username)showLogin(true);}else box.querySelector('[role=alert]').textContent=r.error;}finally{button.disabled=false;}};box.showModal();}

async function renderMemberActivities(){const version=renderVersion,d=await api('GET','/api/activities');if(version!==renderVersion)return;main.innerHTML=`<h2>活动报名</h2><p class="muted">报名、候补与签到均归属当前登录成员。签到成功增加 10 积分。</p>${(d.activities||[]).map(a=>{const signed=a.signedUp.includes(ME),waiting=a.waitlist.includes(ME),checked=a.checkedIn.includes(ME);return `<section class="card"><h3>${esc(a.name)} <span class="tag">${a.closed?'已结束':checked?'已签到':signed?'已报名':waiting?'候补中':'可报名'}</span></h3><p>${activityTime(a)} · ${a.signedCount} / ${a.capacity} 人 · 候补 ${a.waitingCount} 人</p><p class="muted">所需标签：${a.requiredTags.map(esc).join('、')||'不限'}</p><div class="row"><button class="btn" ${a.closed||signed||waiting?'disabled':''} onclick="signup(${a.id})">报名</button><button class="btn gray" ${a.closed||checked||(!signed&&!waiting)?'disabled':''} onclick="cancelA(${a.id})">取消参与</button><button class="btn" ${a.closed||checked||!signed?'disabled':''} onclick="checkin(${a.id})">签到 +10</button></div></section>`;}).join('')||'<div class="card empty">暂无活动</div>'}`;}
async function renderMemberClubs(){const version=renderVersion,[d,p]=await Promise.all([api('GET','/api/clubs'),api('GET','/api/applications')]);if(version!==renderVersion)return;main.innerHTML=`<h2>社团入社</h2><p class="muted">选择感兴趣的社团提交申请，由管理员审核。</p>${(d.clubs||[]).map(c=>{const joined=c.memberIds.includes(ME),pending=(p.applications||[]).some(a=>a.clubId===c.id&&a.status===0);return `<section class="card"><h3>${esc(c.name)} <span class="tag">${esc(c.category||'社团')}</span></h3><p>${esc(c.description||'欢迎加入我们')}</p><button class="btn" ${joined||pending?'disabled':''} onclick="applyClub(${c.id})">${joined?'已加入':pending?'申请待审核':'申请入社'}</button></section>`;}).join('')||'<div class="card empty">暂无社团</div>'}`;}
async function applyClub(id){const r=await api('POST','/api/applications',{clubId:id});if(r.ok)toast('申请已提交，请等待审核');render();}
async function renderMemberApps(){const version=renderVersion,[d,c]=await Promise.all([api('GET','/api/applications'),api('GET','/api/clubs')]);if(version!==renderVersion)return;const names=new Map((c.clubs||[]).map(v=>[v.id,v.name]));main.innerHTML=`<h2>我的入社申请</h2>${(d.applications||[]).slice().reverse().map(a=>`<section class="card"><h3>${esc(names.get(a.clubId)||'社团 #'+a.clubId)}</h3><p>申请 #${a.id} <span class="tag">${({0:'待审核',1:'已通过',[-1]:'已拒绝',[-2]:'已撤回'})[a.status]||'未知'}</span></p><button class="btn gray" ${a.status!==0?'disabled':''} onclick="withdraw(${a.id})">撤回申请</button></section>`).join('')||'<div class="card empty">还没有提交入社申请</div>'}`;}
async function renderMemberAnnouncements(){const version=renderVersion,d=await api('GET','/api/announcements/member/'+ME);if(version!==renderVersion)return;main.innerHTML=`<h2>我的社团公告</h2>${(d.announcements||[]).map(a=>`<section class="card"><span class="muted">社团 #${a.clubId}</span><p style="white-space:pre-wrap">${esc(a.message)}</p></section>`).join('')||'<div class="card empty">暂无公告</div>'}`;}

function memberOptions(id){return '<option value="-1">不绑定（仅管理员）</option>'+members.filter(m=>m.active).map(m=>`<option value="${m.id}" ${m.id===id?'selected':''}>${esc(m.name)} (#${m.id})</option>`).join('');}
async function renderAccounts(){const version=renderVersion,d=await api('GET','/api/accounts');if(version!==renderVersion)return;main.innerHTML=`<h2>账号与权限</h2><p class="muted">普通成员必须绑定在册成员。修改权限、绑定或启用状态后，该账号需要重新登录。</p><section class="card"><h3>创建账号</h3><form id="account_create"><div class="row"><label class="field">账号<input name="username" autocomplete="off" pattern="[A-Za-z0-9_.-]{3,32}" placeholder="3–32 位英文或数字" required></label><label class="field">初始密码<input name="password" type="password" autocomplete="new-password" minlength="10" maxlength="128" required></label><label class="field">角色<select name="role"><option value="member">普通成员</option><option value="admin">管理员</option></select></label><label class="field">绑定成员<select name="memberId">${memberOptions(-1)}</select></label><button class="btn" type="submit">创建账号</button></div><p role="alert"></p></form></section><div class="two-col">${(d.accounts||[]).map(a=>`<section class="card"><h3>${esc(a.username)} <span class="tag">${a.role==='admin'?'管理员':'普通成员'}${!a.enabled?' · 已停用':''}</span></h3><form data-account="${esc(a.username)}"><div class="row"><label class="field">角色<select name="role"><option value="member" ${a.role==='member'?'selected':''}>普通成员</option><option value="admin" ${a.role==='admin'?'selected':''}>管理员</option></select></label><label class="field">绑定成员<select name="memberId">${memberOptions(a.memberId)}</select></label><label class="field">状态<select name="enabled"><option value="true" ${a.enabled?'selected':''}>启用</option><option value="false" ${!a.enabled?'selected':''}>停用</option></select></label></div>${a.memberId>=0&&!a.bindingValid?'<p style="color:#b91c1c">原绑定已失效，请核实成员后重新绑定。成员账号在此期间无法登录。</p>':''}<div class="account-actions"><button class="btn" type="submit">保存权限</button><button class="btn gray" type="button" data-reset="${esc(a.username)}">重置密码</button></div><p role="alert"></p></form></section>`).join('')}</div>`;
  const create=$('#account_create');create.onsubmit=async e=>{e.preventDefault();const button=create.querySelector('button');button.disabled=true;try{const r=await api('POST','/api/accounts',Object.fromEntries(new FormData(create)));if(r.ok){create.reset();toast('账号已创建');render();}else create.querySelector('[role=alert]').textContent=r.error;}finally{button.disabled=false;}};
  main.querySelectorAll('[data-account]').forEach(form=>form.onsubmit=async e=>{e.preventDefault();const username=form.dataset.account,r=await api('POST','/api/accounts/'+encodeURIComponent(username),Object.fromEntries(new FormData(form)));if(r.ok){toast('账号权限已保存');if(username===USER?.username)showLogin(true);else render();}else form.querySelector('[role=alert]').textContent=r.error;});main.querySelectorAll('[data-reset]').forEach(b=>b.onclick=()=>passwordDialog(b.dataset.reset));
}

function activityTime(a){const hm=m=>String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0');return a.timed?'周'+['日','一','二','三','四','五','六'][a.day]+' '+hm(a.startMin)+'–'+hm(a.endMin):esc(a.time||'时间待定');}
function filterTable(query,id){const q=query.trim().toLocaleLowerCase();document.querySelectorAll('#'+id+' tr').forEach(row=>row.hidden=!row.textContent.toLocaleLowerCase().includes(q));}
async function updateHistory(){const r=await api('GET','/api/state');$('#undo').disabled=!r.canUndo;$('#redo').disabled=!r.canRedo;}
async function importCsv(){const f=$('#csv_file').files[0];if(!f){toast('请先选择 CSV 文件',false);return;}if(f.size>200000){toast('文件请控制在 200 KB 内',false);return;}const r=await api('POST','/api/import/members',{csv:await f.text()});if(r.ok){$('#import_result').textContent=`新增 ${r.added} 人，重名跳过 ${r.skippedDup} 行，格式错误 ${r.skippedInvalid} 行。点击刷新数据查看名单。`;await refreshMe();updateHistory();}}
async function renderDashboard(){const version=renderVersion;
  const [r,d,b]=await Promise.all([api('GET','/api/stats'),api('GET','/api/activities'),api('GET','/api/rank/leaderboard?k=5')]);
  const s=r.data||{},acts=(d.activities||[]).filter(a=>!a.closed);
  if(version!==renderVersion)return;main.innerHTML=`<div class="hero"><span class="tag">CLUB MANAGER</span><h2 style="margin-top:12px">把社团日常，安排得井井有条</h2><p>管理成员、组织活动、审核申请，也记录每一次参与。选择上方身份，查看自己的日程与签到。</p></div>
    <div class="metrics">${[['在册成员',s.members],['社团 / 部门',s.clubs],['开放活动',(s.activities||0)-(s.closedActivities||0)],['待审申请',s.pending],['正取 / 候补',`${s.registrations||0} / ${s.waiting||0}`],['累计签到',s.checkIns]].map(([label,value])=>`<div class="metric"><span>${label}</span><strong>${value??0}</strong></div>`).join('')}</div>
    <div class="two-col"><section class="card"><h3>活动报名进度</h3>${acts.length?acts.slice(0,8).map(a=>`<p><strong>${esc(a.name)}</strong> <span class="muted">${a.signedUp.length} / ${a.capacity} 人 · 候补 ${a.waitlist.length}</span></p><progress max="${Math.max(1,a.capacity)}" value="${a.signedUp.length}" aria-label="${esc(a.name)}报名进度"></progress>`).join(''):'<p class="empty">还没有开放活动，去活动页面创建第一场。</p>'}</section>
    <section class="card"><h3>活跃成员 Top 5</h3>${(b.board||[]).length?(b.board||[]).map((p,i)=>`<p><span class="tag">${i+1}</span> ${nameOf(p[1])}<strong style="float:right">${p[0]} 分</strong></p>`).join(''):'<p class="empty">从添加第一位成员开始。</p>'}<p class="muted">累计积分 ${s.totalScore||0} · 已结束活动 ${s.closedActivities||0}</p></section></div>`;
}
async function renderSchedule(){const version=renderVersion;
  if(!members.some(m=>m.active)){if(version!==renderVersion)return;main.innerHTML='<h2>我的日程</h2><div class="card empty">请先添加成员，再选择你的身份。</div>';return;}
  const d=await api('GET',`/api/schedule/${ME}`),list=d.activities||[];
  if(version!==renderVersion)return;main.innerHTML=`<h2>${nameOf(ME)}的活动日程</h2><p class="muted">按星期和开始时间排序；待定时间及已结束活动排在后面。候补不占用正式日程，转正时重新检测冲突。</p>${list.length?list.map(a=>{const signed=a.signedUp.includes(ME),checked=a.checkedIn.includes(ME);return `<section class="card"><div class="row"><h3 style="margin:0">${esc(a.name)}</h3><span class="tag">${a.closed?'已结束':checked?'已签到':signed?'已报名':'候补中'}</span><span class="muted">${activityTime(a)}</span></div><div class="row" style="margin-top:14px"><button class="btn" ${!signed||checked||a.closed?'disabled':''} onclick="checkin(${a.id})">签到 +10</button><button class="btn gray" ${checked||a.closed?'disabled':''} onclick="cancelA(${a.id})">取消参与</button></div></section>`;}).join(''):'<div class="card empty">暂无参与的活动，去活动页面报名吧。</div>'}`;
}

/* ---- 成员 ---- */
async function renderMembers(){const version=renderVersion;
  const d=await api('GET','/api/members');members=d.members||[];buildPicker();
  if(version!==renderVersion)return;main.innerHTML=`
  <h2>成员管理</h2>
  <div class="card"><div class="row"><input id="member_search" aria-label="搜索成员" placeholder="搜索姓名、年级、标签" oninput="filterTable(this.value,'member_rows')">
    <a class="download" href="/api/export/members" download="members.csv">导出成员 CSV</a></div></div>
  <details class="card"><summary>批量导入成员</summary><p class="muted">UTF-8 CSV：姓名,年级,标签1;标签2。支持表头、逗号引号；逐行显示导入统计。</p><input type="file" id="csv_file" accept=".csv,.txt" aria-label="成员 CSV 文件"><button class="btn" onclick="importCsv()">导入</button><p id="import_result" role="status"></p></details>
  <div class="card"><h3>添加成员</h3><div class="row">
    <input id="m_name" placeholder="姓名"><input id="m_grade" placeholder="年级" style="width:90px">
    <input id="m_tags" placeholder="标签(逗号分隔) 如 Python,算法" style="width:240px">
    <button class="btn" onclick="addMember()">添加</button></div></div>
  <div class="card"><h3>按标签查（倒排索引）</h3><div class="row">
    <input id="q_tag" placeholder="标签" style="width:120px"><button class="btn" onclick="queryTag('one')">单词查询</button>
    <span class="muted">多标签:</span><input id="q_tags" placeholder="Python,算法" style="width:150px">
    <select id="q_mode"><option value="all">同时具备(AND)</option><option value="any">任一具备(OR)</option></select>
    <button class="btn" onclick="queryTag('multi')">查询</button></div>
    <div id="qres" class="muted" style="margin-top:8px"></div></div>
  <table><thead><tr><th>ID</th><th>姓名</th><th>年级</th><th>活跃度</th><th>标签</th><th>操作</th></tr></thead><tbody id="member_rows">
  ${members.filter(m=>m.active).map(m=>`<tr style="${m.active?'':'opacity:.45'}"><td>${m.id}</td><td>${esc(m.name)}</td><td>${esc(m.grade)}</td><td>${m.score}</td>
    <td>${(m.tags||[]).map(t=>`<span class="tag">${esc(t)}</span>`).join('')}</td>
    <td><button class="btn sm" onclick="bump(${m.id},10)">+10</button>
        <button class="btn sm gray" onclick="bump(${m.id},-10)">-10</button>
        <button class="btn sm gray" onclick="addTag(${m.id})">+标签</button>
        <button class="btn sm gray" onclick="delTag(${m.id})">-标签</button>
        <button class="btn sm danger" onclick="delMember(${m.id})">删除</button></td></tr>`).join('')}
  </tbody></table>`;
}
async function addMember(){const name=$('#m_name').value.trim(),grade=$('#m_grade').value.trim(),tags=$('#m_tags').value.trim();if(!name){toast('请填姓名',false);return;}const r=await api('POST','/api/members',{name,grade,tags});toast(r.ok?('已添加 成员#'+r.data.id):(r.error||'失败'),r.ok);refreshMe().then(render);}
async function bump(id,d){const r=await api('POST',`/api/members/${id}/score`,{delta:d});toast(r.ok?('活跃度 '+(d>0?'+':'')+d):(r.error||'失败'),r.ok);render();}
async function addTag(id){const t=prompt('输入要添加的标签:');if(!t)return;const r=await api('POST',`/api/members/${id}/tag`,{tag:t});toast(r.ok?'已添加标签':(r.error||'失败'),r.ok);render();}
async function delTag(id){const t=prompt('输入要删除的标签:');if(!t)return;const r=await api('POST',`/api/members/${id}/untag`,{tag:t});toast(r.ok?'已删除标签':(r.error||'失败'),r.ok);render();}
async function delMember(id){if(!confirm('确认删除该成员?'))return;const r=await api('DELETE',`/api/members/${id}`);toast(r.ok?'已删除':(r.error||'失败'),r.ok);refreshMe().then(render);}
function nameOf(id){const m=members.find(x=>x.id===id);return m?esc(m.name):('#'+id);}
async function queryTag(mode){const res=$('#qres');if(mode==='one'){const tag=$('#q_tag').value.trim();if(!tag)return;const d=await api('GET','/api/members/bytag?tag='+encodeURIComponent(tag));res.innerHTML='具备「'+esc(tag)+'」的成员：'+((d.members||[]).map(nameOf).join('、')||'无');}else{const tags=$('#q_tags').value.trim();if(!tags)return;const m=$('#q_mode').value;const d=await api('GET','/api/members/bytags?mode='+m+'&tags='+encodeURIComponent(tags));res.innerHTML='结果：'+((d.members||[]).map(nameOf).join('、')||'无');}}

/* ---- 社团 ---- */
async function renderClubs(){const version=renderVersion;
  const c=await api('GET','/api/clubs');buildPicker();
  const clubs=c.clubs||[];
  if(version!==renderVersion)return;main.innerHTML=`
  <h2>社团与组织架构</h2>
  <div class="card"><h3>创建社团 / 子部门</h3><div class="row">
    <input id="c_name" placeholder="名称"><input id="c_cat" placeholder="分类" style="width:110px">
    <input id="c_found" placeholder="成立日期" style="width:110px"><input id="c_desc" placeholder="简介" style="width:180px">
    <input id="c_parent" placeholder="父ID(顶层留空)" style="width:130px"><button class="btn" onclick="addClub()">创建</button></div></div>
  <div class="card"><h3>组织架构</h3><button class="btn" onclick="showOrg()">查看架构图</button><pre id="org" class="muted" style="margin-top:8px"></pre></div>
  <table><thead><tr><th>ID</th><th>名称</th><th>分类</th><th>成立</th><th>父</th><th>成员数</th><th>操作</th></tr></thead><tbody>
  ${clubs.map(c=>`<tr><td>${c.id}</td><td>${esc(c.name)}</td><td>${esc(c.category)}</td><td>${esc(c.founded)}</td>
    <td>${c.parentClub<0?'—':c.parentClub}</td><td>${(c.memberIds||[]).length}</td>
    <td><button class="btn sm" onclick="joinClub(${c.id})">入社</button>
        <button class="btn sm gray" onclick="leaveClub(${c.id})">退社</button>
        <button class="btn sm danger" onclick="delClub(${c.id})">删除</button></td></tr>`).join('')}
  </tbody></table>`;
}
async function addClub(){const name=$('#c_name').value.trim();if(!name){toast('请填名称',false);return;}const parent=$('#c_parent').value.trim();const r=await api('POST','/api/clubs',{name,category:$('#c_cat').value.trim(),founded:$('#c_found').value.trim(),description:$('#c_desc').value.trim(),parentId:parent});toast(r.ok?('已创建 社团#'+r.data.id):(r.error||'失败'),r.ok);render();}
async function joinClub(cid){const r=await api('POST',`/api/clubs/${cid}/join`,{memberId:ME});toast(r.ok?'已入社':(r.error||'失败'),r.ok);render();}
async function leaveClub(cid){const r=await api('POST',`/api/clubs/${cid}/leave`,{memberId:ME});toast(r.ok?'已退社':(r.error||'失败'),r.ok);render();}
async function delClub(cid){if(!confirm('确认删除该社团?'))return;const r=await api('DELETE',`/api/clubs/${cid}`);toast(r.ok?'已删除':(r.error||'失败'),r.ok);render();}
async function showOrg(){const d=await api('GET','/api/clubs/org');$('#org').textContent=(d.lines||[]).join('\n')||'（空）';}

/* ---- 活动 ---- */
async function renderActivities(){const version=renderVersion;
  const a=await api('GET','/api/activities'),c=await api('GET','/api/clubs');buildPicker();
  const acts=a.activities||[];
  if(version!==renderVersion)return;main.innerHTML=`
  <h2>活动管理</h2><div class="card row"><input aria-label="搜索活动" placeholder="搜索活动、社团或状态" oninput="filterTable(this.value,'activity_rows')"><span class="muted">签到奖励 10 分；已结束活动保留名单与签到记录。</span></div>
  <div class="card"><h3>创建活动</h3><div class="row">
    <input id="a_club" placeholder="社团ID" style="width:80px"><input id="a_name" placeholder="活动名称">
    <input id="a_time" placeholder="时间(展示用)" style="width:130px"><input id="a_cap" placeholder="容量" style="width:70px">
    <button class="btn" onclick="addAct(false)">创建</button></div></div>
  <div class="card"><h3>创建定时活动（带时间段+所需标签，可检测时间冲突）</h3><div class="row">
    <input id="t_club" placeholder="社团ID" style="width:80px"><input id="t_name" placeholder="名称">
    <input id="t_cap" placeholder="容量" style="width:70px"><input id="t_day" placeholder="日0-6" style="width:70px">
    <input id="t_start" placeholder="起(分)" style="width:80px"><input id="t_end" placeholder="止(分)" style="width:80px">
    <input id="t_tags" placeholder="所需标签" style="width:140px"><button class="btn" onclick="addAct(true)">创建</button></div></div>
  <table><thead><tr><th>ID</th><th>名称</th><th>社团</th><th>时间</th><th>容量</th><th>正取 / 候补 / 签到</th><th>操作</th></tr></thead><tbody id="activity_rows">
  ${acts.map(a=>`<tr><td>${a.id}</td><td>${esc(a.name)}</td><td>${esc((c.clubs||[]).find(x=>x.id===a.clubId)?.name||a.clubId)}</td><td>${activityTime(a)}<br><span class="tag">${a.closed?'已结束':'报名中'}</span></td><td>${a.capacity}</td>
    <td>${(a.signedUp||[]).length} / ${(a.waitlist||[]).length} / ${(a.checkedIn||[]).length}</td>
    <td><button class="btn sm" ${a.closed?'disabled':''} onclick="signup(${a.id})">报名</button>
        <button class="btn sm" ${a.closed?'disabled':''} onclick="checkin(${a.id})">我的签到</button>
        <button class="btn sm gray" onclick="showRoster(${a.id})">名单</button>
        <a class="download" href="/api/activities/${a.id}/export" download="activity-${a.id}.csv">导出</a>
        <button class="btn sm gray" ${a.closed?'disabled':''} onclick="closeAct(${a.id})">结束</button>
        <button class="btn sm gray" onclick="cancelA(${a.id})">取消</button>
        <button class="btn sm gray" onclick="setCap(${a.id})">改容量</button>
        <button class="btn sm danger" onclick="delAct(${a.id})">删除</button></td></tr>`).join('')}
  </tbody></table>`;
}
async function checkin(aid){const r=await api('POST',`/api/activities/${aid}/checkin`,{memberId:ME});toast(r.ok?'签到成功，活跃度 +10':r.error,r.ok);await refreshMe();render();}
async function closeAct(aid){if(!confirm('结束活动后停止报名、取消和签到，历史名单仍保留。确认结束？'))return;const r=await api('POST',`/api/activities/${aid}/close`);toast(r.ok?'活动已结束':r.error,r.ok);render();}
async function showRoster(aid){const d=await api('GET','/api/activities'),a=(d.activities||[]).find(x=>x.id===aid);if(!a)return;const w=await api('GET',`/api/activities/${aid}/waitlist`);const box=document.createElement('dialog');box.innerHTML=`<h2>${esc(a.name)} · 名单</h2><h3>正取</h3><p>${a.signedUp.map(id=>nameOf(id)+(a.checkedIn.includes(id)?' ✓ 已签到':'')).join('、')||'暂无'}</p><h3>候补（当前积分优先，同分 ID 小者优先）</h3><p>${(w.ids||[]).map(nameOf).join('、')||'暂无'}</p><button class="btn">关闭</button>`;document.body.append(box);box.querySelector('button').onclick=()=>box.close();box.onclose=()=>box.remove();box.showModal();}
async function addAct(timed){let r;if(timed){r=await api('POST','/api/activities',{clubId:$('#t_club').value.trim(),name:$('#t_name').value.trim(),capacity:$('#t_cap').value.trim(),day:$('#t_day').value.trim(),startMin:$('#t_start').value.trim(),endMin:$('#t_end').value.trim(),requiredTags:$('#t_tags').value.trim()});}else{r=await api('POST','/api/activities',{clubId:$('#a_club').value.trim(),name:$('#a_name').value.trim(),time:$('#a_time').value.trim(),capacity:$('#a_cap').value.trim()});}toast(r.ok?('已创建 活动#'+r.data.id):(r.error||'失败'),r.ok);render();}
async function signup(aid){const r=await api('POST',`/api/activities/${aid}/signup`,{memberId:ME});toast((r.data&&r.data.message)||(r.ok?'已报名':(r.error||'失败')),r.ok);render();}
async function cancelA(aid){const r=await api('POST',`/api/activities/${aid}/cancel`,{memberId:ME});toast(r.ok?'已取消':(r.error||'失败'),r.ok);render();}
async function setCap(aid){const v=prompt('新容量:');if(v==null)return;const r=await api('POST',`/api/activities/${aid}/capacity`,{newCapacity:v});toast(r.ok?'已调整':(r.error||'失败'),r.ok);render();}
async function delAct(aid){if(!confirm('确认删除该活动?'))return;const r=await api('DELETE',`/api/activities/${aid}`);toast(r.ok?'已删除':(r.error||'失败'),r.ok);render();}

/* ---- 审批 ---- */
async function renderApps(){const version=renderVersion;
  const a=await api('GET','/api/applications');buildPicker();
  const apps=a.applications||[],st={0:'待审',1:'通过',[-1]:'拒绝',[-2]:'撤回'};
  if(version!==renderVersion)return;main.innerHTML=`
  <h2>入社审批（优先级队列）</h2>
  <div class="card"><h3>提交申请（以我的身份）</h3><div class="row">
    <input id="p_club" placeholder="社团ID" style="width:90px"><input id="p_prio" placeholder="优先级(0普通/1骨干/2加急)" style="width:220px">
    <button class="btn" onclick="submitApp()">提交</button>
    <button class="btn gray" onclick="processApp()">查看下一条待审</button>
    <span class="muted" id="pend"></span></div></div>
  <table><thead><tr><th>ID</th><th>成员</th><th>社团</th><th>优先级</th><th>状态</th><th>操作</th></tr></thead><tbody>
  ${apps.map(a=>`<tr><td>${a.id}</td><td>${nameOf(a.memberId)}</td><td>${a.clubId}</td><td>${a.priority}</td>
    <td>${st[a.status]||a.status}</td>
    <td><button class="btn sm" ${a.status!==0?'disabled':''} onclick="review(${a.id},true)">通过</button>
        <button class="btn sm danger" ${a.status!==0?'disabled':''} onclick="review(${a.id},false)">拒绝</button> <button class="btn sm gray" ${a.status!==0||a.memberId!==ME?'disabled':''} onclick="withdraw(${a.id})">撤回</button></td></tr>`).join('')}
  </tbody></table>`;
  const p=await api('GET','/api/applications/pending');if(version!==renderVersion)return;$('#pend').textContent='待处理: '+(p.count||0);
}
async function submitApp(){const r=await api('POST','/api/applications',{memberId:ME,clubId:$('#p_club').value.trim(),priority:$('#p_prio').value.trim()});toast(r.ok?('已提交 申请#'+r.data.id):(r.error||'失败'),r.ok);render();}
async function processApp(){const r=await api('POST','/api/applications/process');toast(r.ok?('下一条待审：申请#'+r.data.id+'，请点击通过或拒绝'):(r.error||'无待审'),r.ok);render();}
async function withdraw(id){const r=await api('POST',`/api/applications/${id}/withdraw`,{memberId:ME});toast(r.ok?'已撤回':r.error,r.ok);render();}
async function review(id,ok){const r=await api('POST',`/api/applications/${id}/review`,{approve:ok});toast(r.ok?(ok?'已通过':'已拒绝'):(r.error||'失败'),r.ok);render();}

/* ---- 好友 ---- */
async function renderFriends(){const version=renderVersion;buildPicker();if(version!==renderVersion)return;main.innerHTML=`
  <h2>好友关系（图）</h2>
  <div class="card"><h3>添加 / 删除好友</h3><div class="row">
    <input id="f_a" placeholder="成员A" style="width:90px"><input id="f_b" placeholder="成员B" style="width:90px">
    <button class="btn" onclick="addFriend()">加好友</button>
    <button class="btn danger" onclick="delFriend()">删好友</button></div></div>
  <div class="card"><h3>查询</h3><div class="row">
    <button class="btn" onclick="recFriends()">推荐我的二度人脉</button>
    <input id="q_a" placeholder="A" style="width:70px"><input id="q_b" placeholder="B" style="width:70px">
    <button class="btn" onclick="dist()">最短距离</button>
    <button class="btn" onclick="pathQ()">最短路径</button>
    <button class="btn gray" onclick="recAct()">好友活动推荐(我)</button></div>
    <div id="qres" class="muted" style="margin-top:8px"></div></div>`;
}
async function addFriend(){const r=await api('POST','/api/friends',{a:$('#f_a').value.trim(),b:$('#f_b').value.trim()});toast(r.ok?'已加好友':(r.error||'失败'),r.ok);render();}
async function delFriend(){const r=await api('DELETE','/api/friends',{a:$('#f_a').value.trim(),b:$('#f_b').value.trim()});toast(r.ok?'已删除':(r.error||'失败'),r.ok);render();}
async function recFriends(){const d=await api('GET',`/api/friends/${ME}/recommend`);$('#qres').innerHTML='推荐：'+((d.recs||[]).map(r=>nameOf(r.memberId)+`(共同${r.mutualFriends})`).join('、')||'无');}
async function dist(){const d=await api('GET',`/api/friends/${$('#q_a').value.trim()}/distance/${$('#q_b').value.trim()}`);$('#qres').innerHTML='最短距离：'+(d.data&&d.data.distance!=null?d.data.distance+' 跳':'—');}
async function pathQ(){const d=await api('GET',`/api/friends/${$('#q_a').value.trim()}/path/${$('#q_b').value.trim()}`);$('#qres').innerHTML='最短路径：'+((d.data&&d.data.path||[]).map(nameOf).join(' → ')||'—');}
async function recAct(){const d=await api('GET',`/api/friends/${ME}/recact`);$('#qres').innerHTML='好友推荐活动：'+((d.recs||[]).map(r=>`活动#${r.actId}(热度${r.score})`).join('、')||'无');}

/* ---- 排行 ---- */
async function renderRank(){const version=renderVersion;if(version!==renderVersion)return;main.innerHTML=`
  <h2>排行榜（AVL）</h2>
  <div class="card"><h3>查询</h3><div class="row">
    <input id="r_k" placeholder="Top-K" style="width:70px" value="10"><button class="btn" onclick="board()">活跃榜</button>
    <input id="r_id" placeholder="成员ID" style="width:80px" value="${ME}"><button class="btn" onclick="myRank()">个人排名</button>
    <input id="r_lo" placeholder="低" style="width:60px"><input id="r_hi" placeholder="高" style="width:60px"><button class="btn" onclick="rangeQ()">区间人数</button>
    <button class="btn gray" onclick="clubBoard()">社团活跃榜</button></div>
  <div id="rres" class="card"><span class="muted">点击上方按钮查询</span></div>`;}
async function board(){const d=await api('GET','/api/rank/leaderboard?k='+($('#r_k').value||10));$('#rres').innerHTML='<h3>活跃榜</h3>'+((d.board||[]).map((p,i)=>`${i+1}. 成员#${p[1]} — ${p[0]} 分`).join('<br>')||'空');}
async function myRank(){const d=await api('GET',`/api/rank/member/${$('#r_id').value||ME}`);$('#rres').innerHTML=`成员#${esc($('#r_id').value)} 排名：${d.data&&d.data.rank!=null?(d.data.rank<0?'未找到':d.data.rank+1):'—'}`;}
async function rangeQ(){const d=await api('GET',`/api/rank/range?lo=${$('#r_lo').value}&hi=${$('#r_hi').value}`);$('#rres').innerHTML=`活跃度区间内人数：${d.data&&d.data.count!=null?d.data.count:'—'}`;}
async function clubBoard(){const d=await api('GET','/api/rank/clubleader?k=10');$('#rres').innerHTML='<h3>社团活跃榜</h3>'+((d.board||[]).map((p,i)=>`${i+1}. 社团#${p[1]} — ${p[0]} 分`).join('<br>')||'空');}

/* ---- 公告 ---- */
async function renderAnnounce(){const version=renderVersion;buildPicker();if(version!==renderVersion)return;main.innerHTML=`
  <h2>公告广播</h2>
  <div class="card"><h3>发布公告（以我的身份，发到我所在社团）</h3><div class="row">
    <input id="an_club" placeholder="社团ID" style="width:90px"><input id="an_msg" placeholder="公告内容" style="width:300px">
    <button class="btn" onclick="broadcast()">广播</button></div></div>
  <div class="card"><h3>查看</h3><div class="row">
    <input id="an_cid" placeholder="社团ID" style="width:90px"><button class="btn" onclick="annClub()">某社团公告</button>
    <button class="btn gray" onclick="annMine()">我能收到的公告</button></div></div>
  <div id="anres" class="card"><span class="muted">点击上方按钮查看</span></div>`;}
async function broadcast(){const r=await api('POST','/api/announcements',{clubId:$('#an_club').value.trim(),senderId:ME,message:$('#an_msg').value.trim()});toast(r.ok?('已发 公告#'+r.data.id):(r.error||'失败'),r.ok);}
async function annClub(){const d=await api('GET','/api/announcements/club/'+$('#an_cid').value.trim());showAnn(d.announcements||[]);}
async function annMine(){const d=await api('GET','/api/announcements/member/'+ME);showAnn(d.announcements||[]);}
function showAnn(list){$('#anres').innerHTML='<h3>公告</h3>'+(list.map(a=>`[社团${a.clubId}] ${esc(a.message)} <span class="muted">#${a.id}</span>`).join('<br>')||'（无）');}

$('#undo').onclick=async()=>{const r=await api('POST','/api/undo');toast(r.ok?'已撤销':(r.error||'无可撤销'),r.ok);render();};
$('#redo').onclick=async()=>{const r=await api('POST','/api/redo');toast(r.ok?'已重做':(r.error||'无可重做'),r.ok);render();};
$('#refresh').onclick=async()=>{await refreshMe();render();};
$('#save').onclick=async()=>{const r=await api('POST','/api/save');toast(r.ok?'已保存':'保存失败',r.ok);};
$('#logout').onclick=async()=>{const r=await api('POST','/api/auth/logout');if(r.ok)showLogin(true);};
$('#password').onclick=()=>passwordDialog();
boot().catch(()=>{main.innerHTML='<section class="card empty">无法连接服务，请刷新页面重试。</section>';});
window.addEventListener('unhandledrejection',e=>{e.preventDefault();});
</script>
</body>
</html>)CLUBGUI";

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
static std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = { s[i + 1], s[i + 2], 0 };
            out.push_back((char)strtol(hex, nullptr, 16));
            i += 2;
        } else {
            out.push_back(s[i] == '+' ? ' ' : s[i]);
        }
    }
    return out;
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out.push_back(c);   // 多字节 UTF-8（中文）原样保留
                }
        }
    }
    return out;
}

static int toInt(const std::string& s) {
    size_t used = 0; int value = std::stoi(s, &used);
    if (used != s.size()) throw std::invalid_argument("invalid integer");
    return value;
}

// 按 , ， ; ； 空格 切分标签
static std::vector<std::string> splitTags(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == ';' || c == ' ' || c == '\t') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// application/x-www-form-urlencoded 解析（支持重复键）
typedef std::map<std::string, std::vector<std::string>> Form;
static Form parseForm(const std::string& body) {
    Form f;
    size_t i = 0;
    while (i < body.size()) {
        size_t amp = body.find('&', i);
        std::string pair = body.substr(i, amp == std::string::npos ? std::string::npos : amp - i);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string k = urlDecode(pair.substr(0, eq));
            std::string v = urlDecode(pair.substr(eq + 1));
            f[k].push_back(v);
        }
        if (amp == std::string::npos) break;
        i = amp + 1;
    }
    return f;
}
static std::string formGet(const Form& f, const std::string& k, const std::string& def = "") {
    auto it = f.find(k);
    if (it != f.end() && !it->second.empty()) return it->second[0];
    return def;
}

static std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> segs;
    size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') ++i;
        size_t start = i;
        while (i < path.size() && path[i] != '/') ++i;
        if (i > start) segs.push_back(path.substr(start, i - start));
    }
    return segs;
}

static void sendAll(sock_t s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int n = send(s, data.data() + sent, (int)(data.size() - sent), 0);
        if (n <= 0) break;
        sent += (size_t)n;
    }
}

static void httpResponse(sock_t s, int code, const std::string& status,
                         const std::string& contentType, const std::string& body,
                         const std::string& extraHeaders = "") {
    std::ostringstream os;
    os << "HTTP/1.1 " << code << " " << status << "\r\n"
       << "Content-Type: " << contentType << "; charset=utf-8\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Connection: close\r\n"
       << "Cache-Control: no-store\r\n"
       << "X-Content-Type-Options: nosniff\r\n"
       << "X-Frame-Options: DENY\r\n"
       << "Referrer-Policy: same-origin\r\n"
       << extraHeaders
       << "\r\n" << body;
    sendAll(s, os.str());
}

static std::string jOk(const std::string& data)  { return "{\"ok\":true,\"data\":" + data + "}"; }
static std::string jErr(const std::string& msg) { return "{\"ok\":false,\"error\":\"" + jsonEscape(msg) + "\"}"; }

static size_t getContentLength(const std::string& headers) {
    std::string low;
    low.reserve(headers.size());
    for (char c : headers) low.push_back((char)tolower((unsigned char)c));
    size_t pos = low.find("content-length:");
    if (pos == std::string::npos) return 0;
    pos += strlen("content-length:");
    while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) ++pos;
    size_t end = pos;
    while (end < headers.size() && headers[end] >= '0' && headers[end] <= '9') ++end;
    return (size_t)strtoul(headers.substr(pos, end - pos).c_str(), nullptr, 10);
}

// ---------------------------------------------------------------------------
// 序列化（各实体 -> JSON 片段）
// ---------------------------------------------------------------------------
static std::string memberJson(const Member& m) {
    std::ostringstream js;
    js << "{\"id\":" << m.id << ",\"name\":\"" << jsonEscape(m.name)
       << "\",\"grade\":\"" << jsonEscape(m.grade)
       << "\",\"score\":" << m.score
       << ",\"active\":" << (m.active ? "true" : "false") << ",\"tags\":[";
    for (size_t j = 0; j < m.tags.size(); ++j) { if (j) js << ","; js << "\"" << jsonEscape(m.tags[j]) << "\""; }
    js << "]}";
    return js.str();
}
static std::string clubJson(const Club& c, int viewer = -1) {
    std::ostringstream js;
    js << "{\"id\":" << c.id << ",\"name\":\"" << jsonEscape(c.name)
       << "\",\"category\":\"" << jsonEscape(c.category)
       << "\",\"founded\":\"" << jsonEscape(c.founded)
       << "\",\"description\":\"" << jsonEscape(c.description)
       << "\",\"parentClub\":" << c.parentClub << ",\"memberIds\":[";
    bool first = true;
    for (int id : c.memberIds) { if (viewer >= 0 && id != viewer) continue; if (!first) js << ','; first = false; js << id; }
    js << "],\"subClubs\":[";
    for (size_t j = 0; j < c.subClubs.size(); ++j) { if (j) js << ","; js << c.subClubs[j]; }
    js << "]}";
    return js.str();
}
static std::string activityJson(const Activity& a, int viewer = -1) {
    std::ostringstream js;
    js << "{\"id\":" << a.id << ",\"clubId\":" << a.clubId
       << ",\"name\":\"" << jsonEscape(a.name)
       << "\",\"time\":\"" << jsonEscape(a.time)
       << "\",\"capacity\":" << a.capacity
       << ",\"signedCount\":" << a.signedUp.size() << ",\"waitingCount\":" << a.waitlist.size()
       << ",\"signedUp\":[";
    bool first = true;
    for (int id : a.signedUp) { if (viewer >= 0 && id != viewer) continue; if (!first) js << ','; first = false; js << id; }
    js << "],\"requiredTags\":[";
    for (size_t j = 0; j < a.requiredTags.size(); ++j) { if (j) js << ","; js << "\"" << jsonEscape(a.requiredTags[j]) << "\""; }
    js << "],\"waitlist\":[";
    first = true;
    for (const auto& p : a.waitlist.snapshot()) { if (viewer >= 0 && p.second != viewer) continue; if (!first) js << ','; first = false; js << p.second; }
    js << "],\"checkedIn\":[";
    first = true;
    for (int id : a.checkedIn) { if (viewer >= 0 && id != viewer) continue; if (!first) js << ','; first = false; js << id; }
    js << "],\"closed\":" << (a.closed ? "true" : "false");
    js << ",\"day\":" << a.day << ",\"startMin\":" << a.startMin
       << ",\"endMin\":" << a.endMin << ",\"timed\":" << (a.timed() ? "true" : "false") << "}";
    return js.str();
}
static std::string appJson(const Application& a) {
    std::ostringstream js;
    js << "{\"id\":" << a.id << ",\"memberId\":" << a.memberId
       << ",\"clubId\":" << a.clubId
       << ",\"submitSeq\":" << a.submitSeq
       << ",\"priority\":" << a.priority
       << ",\"status\":" << a.status << "}";
    return js.str();
}
static std::string annJson(const Announcement& a) {
    std::ostringstream js;
    js << "{\"id\":" << a.id << ",\"clubId\":" << a.clubId
       << ",\"senderId\":" << a.senderId
       << ",\"message\":\"" << jsonEscape(a.message)
       << "\",\"seq\":" << a.seq << "}";
    return js.str();
}

// 汇总所有社团的公告（系统没有全量 announcements() 访问器，这里聚合）
static std::vector<Announcement> allAnnouncements(ClubSystem& sys) {
    std::vector<Announcement> all;
    for (const auto& c : sys.clubs()) {
        if (!c.active) continue;
        auto v = sys.announcementsOf(c.id);
        for (auto& a : v) all.push_back(a);
    }
    std::sort(all.begin(), all.end(),
              [](const Announcement& a, const Announcement& b) { return a.seq > b.seq; });
    return all;
}

// ---------------------------------------------------------------------------
// 路由：method + path + query + body -> JSON 响应
// ---------------------------------------------------------------------------
#include "web_auth.inc"

static void routeAndRespond(sock_t s, const std::string& method,
                            const std::string& path, const std::string& query,
                            const std::string& body, ClubSystem& sys, AuthStore& auth,
                            const std::map<std::string, std::string>& headers, const std::string& client) {
    std::vector<std::string> seg = splitPath(path);
    Form q = parseForm(query);
    Form f = parseForm(body);
    std::string resp;
    auto saved = [&]() { if (!sys.save(SAVE_FILE)) resp = jErr("操作已生效，但存档失败；请检查目录权限并重新保存"); };   // 写操作后自动存档

    // GET / -> 前端页面
    if (method == "GET" && seg.empty()) {
        httpResponse(s, 200, "OK", "text/html", kHtml);
        return;
    }

    // 默认拒绝；普通成员的全部路由在这里处理，只有管理员进入下方管理路由。
    if (authorizeAndRespond(s, method, seg, f, sys, auth, headers, client)) return;

    if (!seg.empty() && seg[0] == "api") {
        if (seg.size() == 2 && seg[1] == "stats" && method == "GET") {
            auto st = sys.statistics();
            std::ostringstream js;
            js << "{\"members\":" << st.members << ",\"clubs\":" << st.clubs
               << ",\"activities\":" << st.activities << ",\"closedActivities\":" << st.closedActivities
               << ",\"registrations\":" << st.registrations << ",\"waiting\":" << st.waiting
               << ",\"checkIns\":" << st.checkIns << ",\"pending\":" << st.pending
               << ",\"totalScore\":" << st.totalScore << '}';
            resp = jOk(js.str());
        } else if (seg.size() == 3 && seg[1] == "schedule" && method == "GET") {
            int id = toInt(seg[2]);
            if (!sys.findMember(id)) throw std::invalid_argument("member not found");
            std::ostringstream js; js << "{\"activities\":[";
            bool first = true;
            for (int aid : sys.memberSchedule(id)) { if (!first) js << ','; first = false; js << activityJson(*sys.findActivity(aid)); }
            js << "]}"; resp = jOk(js.str());
        } else if (seg.size() == 3 && seg[1] == "export" && seg[2] == "members" && method == "GET") {
            httpResponse(s, 200, "OK", "text/csv", sys.exportMembersCsv()); return;
        } else if (seg.size() == 4 && seg[1] == "activities" && seg[3] == "export" && method == "GET") {
            auto csv = sys.exportRosterCsv(toInt(seg[2]));
            if (csv.empty()) { httpResponse(s, 404, "Not Found", "application/json", jErr("活动不存在")); return; }
            httpResponse(s, 200, "OK", "text/csv", csv); return;
        }
        // ============ 成员 ============
        else if (seg.size() >= 2 && seg[1] == "members") {
            if (seg.size() == 2) {
                if (method == "GET") {
                    std::ostringstream js; js << "{\"members\":[";
                    const auto& ms = sys.members();
                    for (size_t i = 0; i < ms.size(); ++i) { if (i) js << ","; js << memberJson(ms[i]); }
                    js << "]}";
                    resp = jOk(js.str());
                } else if (method == "POST") {  // 添加
                    std::string name = formGet(f, "name"), grade = formGet(f, "grade");
                    std::vector<std::string> tags = splitTags(formGet(f, "tags"));
                    int id = sys.addMember(name, grade, tags);
                    if (id < 0) resp = jErr("添加失败（可能重名）");
                    else { resp = jOk("{\"id\":" + std::to_string(id) + "}"); saved(); }
                }
            } else if (seg[2] == "bytag" && method == "GET") {
                auto v = sys.membersWithTag(formGet(q, "tag"));
                std::ostringstream js; js << "{\"members\":[";
                for (size_t i = 0; i < v.size(); ++i) { if (i) js << ","; js << v[i]; }
                js << "]}"; resp = jOk(js.str());
            } else if (seg[2] == "bytags" && method == "GET") {
                std::vector<std::string> tags = splitTags(formGet(q, "tags"));
                auto v = (formGet(q, "mode") == "all") ? sys.membersWithAllTags(tags) : sys.membersWithAnyTag(tags);
                std::ostringstream js; js << "{\"members\":[";
                for (size_t i = 0; i < v.size(); ++i) { if (i) js << ","; js << v[i]; }
                js << "]}"; resp = jOk(js.str());
            } else if (seg[2] == "tags" && method == "GET") {
                auto v = sys.allTags();
                std::ostringstream js; js << "{\"tags\":[";
                for (size_t i = 0; i < v.size(); ++i) { if (i) js << ","; js << "\"" << jsonEscape(v[i]) << "\""; }
                js << "]}"; resp = jOk(js.str());
            } else if (seg.size() == 3 && method == "DELETE") {  // /api/members/{id}
                bool ok = sys.removeMember(toInt(seg[2]));
                resp = ok ? jOk("{}") : jErr("删除失败（该成员有报名/候补/待审申请）");
                if (ok) saved();
            } else if (seg.size() == 4 && method == "POST") {  // /api/members/{id}/{action}
                int id = toInt(seg[2]);
                if (seg[3] == "score") {
                    if (!sys.findMember(id)) throw std::invalid_argument("member not found");
                    sys.addScore(id, toInt(formGet(f, "delta")));
                    resp = jOk("{}"); saved();
                } else if (seg[3] == "tag") {
                    bool ok = sys.addMemberTag(id, formGet(f, "tag"));
                    resp = ok ? jOk("{}") : jErr("加标签失败"); if (ok) saved();
                } else if (seg[3] == "untag") {
                    bool ok = sys.removeMemberTag(id, formGet(f, "tag"));
                    resp = ok ? jOk("{}") : jErr("删标签失败"); if (ok) saved();
                } else resp = jErr("未知操作");
            } else resp = jErr("未知路由");
        }
        // ============ 批量导入 ============
        else if (seg.size() == 3 && seg[1] == "import" && method == "POST") {
            auto r = sys.importMembers(formGet(f, "csv"));
            std::ostringstream js;
            js << "{\"added\":" << r.added << ",\"skippedDup\":" << r.skippedDup
               << ",\"skippedInvalid\":" << r.skippedInvalid << "}";
            resp = jOk(js.str()); saved();
        }
        // ============ 社团 ============
        else if (seg.size() >= 2 && seg[1] == "clubs") {
            if (seg.size() == 2) {
                if (method == "GET") {
                    std::ostringstream js; js << "{\"clubs\":[";
                    const auto& cs = sys.clubs();
                    bool first = true; for (const auto& c : cs) { if (!c.active) continue; if (!first) js << ","; first = false; js << clubJson(c); }
                    js << "]}"; resp = jOk(js.str());
                } else if (method == "POST") {
                    std::string name = formGet(f, "name");
                    std::string parent = formGet(f, "parentId");
                    int id = parent.empty()
                        ? sys.createClub(name, formGet(f, "category"), formGet(f, "founded"), formGet(f, "description"))
                        : sys.createSubClub(toInt(parent), name, formGet(f, "category"), formGet(f, "founded"), formGet(f, "description"));
                    if (id < 0) resp = jErr("创建失败（可能重名或父不存在）");
                    else { resp = jOk("{\"id\":" + std::to_string(id) + "}"); saved(); }
                }
            } else if (seg.size() == 3 && method == "DELETE") {
                bool ok = sys.removeClub(toInt(seg[2]));
                resp = ok ? jOk("{}") : jErr("删除失败（有在册成员或子部门）"); if (ok) saved();
            } else if (seg.size() == 3 && seg[2] == "org" && method == "GET") {
                auto lines = sys.orgTreeLines();
                std::ostringstream js; js << "{\"lines\":[";
                for (size_t i = 0; i < lines.size(); ++i) { if (i) js << ","; js << "\"" << jsonEscape(lines[i]) << "\""; }
                js << "]}"; resp = jOk(js.str());
            } else if (seg.size() == 4 && seg[3] == "join" && method == "POST") {
                bool ok = sys.joinClub(toInt(seg[2]), toInt(formGet(f, "memberId")));
                resp = ok ? jOk("{}") : jErr("入社失败"); if (ok) saved();
            } else if (seg.size() == 4 && seg[3] == "leave" && method == "POST") {
                bool ok = sys.leaveClub(toInt(seg[2]), toInt(formGet(f, "memberId")));
                resp = ok ? jOk("{}") : jErr("退社失败"); if (ok) saved();
            } else resp = jErr("未知路由");
        }
        // ============ 活动 ============
        else if (seg.size() >= 2 && seg[1] == "activities") {
            if (seg.size() == 2) {
                if (method == "GET") {
                    std::ostringstream js; js << "{\"activities\":[";
                    const auto& as = sys.activities();
                    bool first = true; for (const auto& a : as) { if (!a.active) continue; if (!first) js << ","; first = false; js << activityJson(a); }
                    js << "]}"; resp = jOk(js.str());
                } else if (method == "POST") {
                    int clubId = toInt(formGet(f, "clubId"));
                    std::string name = formGet(f, "name");
                    int cap = toInt(formGet(f, "capacity"));
                    std::string time = formGet(f, "time");
                    std::string rtags = formGet(f, "requiredTags");
                    std::string day = formGet(f, "day"), sm = formGet(f, "startMin"), em = formGet(f, "endMin");
                    int id = -1;
                    if (!day.empty() || !sm.empty() || !em.empty()) {
                        id = sys.addActivity(clubId, name, time, cap, splitTags(rtags),
                                             toInt(day), toInt(sm), toInt(em));
                    } else {
                        id = sys.addActivity(clubId, name, time, cap);
                    }
                    if (id < 0) resp = jErr("创建失败");
                    else { resp = jOk("{\"id\":" + std::to_string(id) + "}"); saved(); }
                }
            } else if (seg.size() == 3 && method == "DELETE") {
                bool ok = sys.removeActivity(toInt(seg[2]));
                resp = ok ? jOk("{}") : jErr("删除失败"); if (ok) saved();
            } else if (seg.size() == 4 && seg[3] == "checkin" && method == "POST") {
                bool ok = sys.checkIn(toInt(formGet(f, "memberId")), toInt(seg[2]));
                resp = ok ? jOk("{}") : jErr("签到失败：仅正取成员可签到一次，活动须未结束且积分未溢出"); if (ok) saved();
            } else if (seg.size() == 4 && seg[3] == "close" && method == "POST") {
                bool ok = sys.closeActivity(toInt(seg[2]));
                resp = ok ? jOk("{}") : jErr("活动不存在或已结束"); if (ok) saved();
            } else if (seg.size() == 4 && seg[3] == "signup" && method == "POST") {
                std::string msg = sys.signup(toInt(formGet(f, "memberId")), toInt(seg[2]));
                bool ok = msg.rfind("成功", 0) == 0;
                resp = ok ? jOk("{\"message\":\"" + jsonEscape(msg) + "\"}") : jErr(msg); if (ok) saved();
            } else if (seg.size() == 4 && seg[3] == "cancel" && method == "POST") {
                bool ok = sys.cancelSignup(toInt(formGet(f, "memberId")), toInt(seg[2]));
                resp = ok ? jOk("{}") : jErr("取消失败"); if (ok) saved();
            } else if (seg.size() == 4 && seg[3] == "capacity" && method == "POST") {
                bool ok = sys.setActivityCapacity(toInt(seg[2]), toInt(formGet(f, "newCapacity")));
                resp = ok ? jOk("{}") : jErr("调整失败"); if (ok) saved();
            } else if (seg.size() == 4 && seg[3] == "waitlist" && method == "GET") {
                auto v = sys.activityWaitlistIds(toInt(seg[2]));
                std::ostringstream js; js << "{\"ids\":[";
                for (size_t i = 0; i < v.size(); ++i) { if (i) js << ","; js << v[i]; }
                js << "]}"; resp = jOk(js.str());
            } else resp = jErr("未知路由");
        }
        // ============ 审批 ============
        else if (seg.size() >= 2 && seg[1] == "applications") {
            if (seg.size() == 2) {
                if (method == "GET") {
                    std::ostringstream js; js << "{\"applications\":[";
                    const auto& as = sys.applications();
                    for (size_t i = 0; i < as.size(); ++i) { if (i) js << ","; js << appJson(as[i]); }
                    js << "]}"; resp = jOk(js.str());
                } else if (method == "POST") {
                    std::string p = formGet(f, "priority");
                    int id = sys.submitApplication(toInt(formGet(f, "memberId")), toInt(formGet(f, "clubId")),
                                                   p.empty() ? 0 : toInt(p));
                    if (id < 0) resp = jErr("提交失败");
                    else { resp = jOk("{\"id\":" + std::to_string(id) + "}"); saved(); }
                }
            } else if (seg.size() == 3 && seg[2] == "process" && method == "POST") {
                int id = sys.processNextApplication();
                if (id < 0) resp = jErr("无待审申请");
                else { resp = jOk("{\"id\":" + std::to_string(id) + "}"); saved(); }
            } else if (seg.size() == 3 && seg[2] == "pending" && method == "GET") {
                resp = jOk("{\"count\":" + std::to_string(sys.pendingCount()) + "}");
            } else if (seg.size() == 4 && seg[3] == "withdraw" && method == "POST") {
                bool ok = sys.withdrawApplication(toInt(seg[2]), toInt(formGet(f, "memberId")));
                resp = ok ? jOk("{}") : jErr("只能撤回本人待审申请"); if (ok) saved();
            } else if (seg.size() == 4 && seg[3] == "review" && method == "POST") {
                if (formGet(f, "approve") != "true" && formGet(f, "approve") != "false") throw std::invalid_argument("invalid approval");
                bool ok = sys.reviewApplication(toInt(seg[2]), formGet(f, "approve") == "true");
                resp = ok ? jOk("{}") : jErr("审批失败"); if (ok) saved();
            } else resp = jErr("未知路由");
        }
        // ============ 好友 ============
        else if (seg.size() >= 2 && seg[1] == "friends") {
            if (seg.size() == 2 && method == "POST") {
                int x = toInt(formGet(f, "a")), y = toInt(formGet(f, "b"));
                if (x == y || !sys.findMember(x) || !sys.findMember(y)) throw std::invalid_argument("invalid friend");
                sys.addFriend(x, y);
                resp = jOk("{}"); saved();
            } else if (seg.size() == 2 && method == "DELETE") {
                bool ok = sys.removeFriend(toInt(formGet(f, "a")), toInt(formGet(f, "b")));
                resp = ok ? jOk("{}") : jErr("删除失败"); if (ok) saved();
            } else if (seg.size() == 4 && seg[3] == "recommend" && method == "GET") {
                auto v = sys.recommendFriends(toInt(seg[2]));
                std::ostringstream js; js << "{\"recs\":[";
                for (size_t i = 0; i < v.size(); ++i) { if (i) js << ","; js << "{\"memberId\":" << v[i].memberId << ",\"mutualFriends\":" << v[i].mutualFriends << "}"; }
                js << "]}"; resp = jOk(js.str());
            } else if (seg.size() == 4 && seg[3] == "recact" && method == "GET") {
                auto v = sys.recommendActivities(toInt(seg[2]));
                std::ostringstream js; js << "{\"recs\":[";
                for (size_t i = 0; i < v.size(); ++i) { if (i) js << ","; js << "{\"actId\":" << v[i].first << ",\"score\":" << v[i].second << "}"; }
                js << "]}"; resp = jOk(js.str());
            } else if (seg.size() == 5 && seg[3] == "distance" && method == "GET") {
                resp = jOk("{\"distance\":" + std::to_string(sys.friendDistance(toInt(seg[2]), toInt(seg[4]))) + "}");
            } else if (seg.size() == 5 && seg[3] == "path" && method == "GET") {
                auto v = sys.friendPath(toInt(seg[2]), toInt(seg[4]));
                std::ostringstream js; js << "{\"path\":[";
                for (size_t i = 0; i < v.size(); ++i) { if (i) js << ","; js << v[i]; }
                js << "]}"; resp = jOk(js.str());
            } else resp = jErr("未知路由");
        }
        // ============ 排行 ============
        else if (seg.size() >= 3 && seg[1] == "rank") {
            if (seg[2] == "leaderboard" && method == "GET") {
                auto v = sys.leaderboard(toInt(formGet(q, "k", "10")));
                std::ostringstream js; js << "{\"board\":[";
                for (size_t i = 0; i < v.size(); ++i) { if (i) js << ","; js << "[" << v[i].first << "," << v[i].second << "]"; }
                js << "]}"; resp = jOk(js.str());
            } else if (seg.size() == 4 && seg[2] == "member" && method == "GET") {
                resp = jOk("{\"rank\":" + std::to_string(sys.memberRank(toInt(seg[3]))) + "}");
            } else if (seg[2] == "range" && method == "GET") {
                resp = jOk("{\"count\":" + std::to_string(sys.rangeActive(toInt(formGet(q, "lo")), toInt(formGet(q, "hi")))) + "}");
            } else if (seg[2] == "clubleader" && method == "GET") {
                auto v = sys.clubLeaderboard(toInt(formGet(q, "k", "10")));
                std::ostringstream js; js << "{\"board\":[";
                for (size_t i = 0; i < v.size(); ++i) { if (i) js << ","; js << "[" << v[i].first << "," << v[i].second << "]"; }
                js << "]}"; resp = jOk(js.str());
            } else resp = jErr("未知路由");
        }
        // ============ 公告 ============
        else if (seg.size() >= 2 && seg[1] == "announcements") {
            if (seg.size() == 2) {
                if (method == "GET") {
                    auto v = allAnnouncements(sys);
                    std::ostringstream js; js << "{\"announcements\":[";
                    for (size_t i = 0; i < v.size(); ++i) { if (i) js << ","; js << annJson(v[i]); }
                    js << "]}"; resp = jOk(js.str());
                } else if (method == "POST") {
                    int id = sys.broadcast(toInt(formGet(f, "clubId")), toInt(formGet(f, "senderId")), formGet(f, "message"));
                    if (id < 0) resp = jErr("广播失败");
                    else { resp = jOk("{\"id\":" + std::to_string(id) + "}"); saved(); }
                }
            } else if (seg.size() == 4 && seg[2] == "club" && method == "GET") {
                auto v = sys.announcementsOf(toInt(seg[3]));
                std::ostringstream js; js << "{\"announcements\":[";
                for (size_t i = 0; i < v.size(); ++i) { if (i) js << ","; js << annJson(v[i]); }
                js << "]}"; resp = jOk(js.str());
            } else if (seg.size() == 4 && seg[2] == "member" && method == "GET") {
                auto v = sys.announcementsForMember(toInt(seg[3]));
                std::ostringstream js; js << "{\"announcements\":[";
                for (size_t i = 0; i < v.size(); ++i) { if (i) js << ","; js << annJson(v[i]); }
                js << "]}"; resp = jOk(js.str());
            } else resp = jErr("未知路由");
        }
        // ============ 撤销 / 重做 / 状态 / 存读档 ============
        else if (seg.size() == 2 && seg[1] == "undo" && method == "POST") {
            bool ok = sys.undo(); resp = ok ? jOk("{}") : jErr("无可撤销"); if (ok) saved();
        } else if (seg.size() == 2 && seg[1] == "redo" && method == "POST") {
            bool ok = sys.redo(); resp = ok ? jOk("{}") : jErr("无可重做"); if (ok) saved();
        } else if (seg.size() == 2 && seg[1] == "state" && method == "GET") {
            resp = jOk(std::string("{\"canUndo\":") + (sys.canUndo() ? "true" : "false") +
                       ",\"canRedo\":" + (sys.canRedo() ? "true" : "false") + "}");
        } else if (seg.size() == 2 && seg[1] == "save" && method == "POST") {
            resp = sys.save(SAVE_FILE) ? jOk("{}") : jErr("保存失败");
        } else if (seg.size() == 2 && seg[1] == "load" && method == "POST") {
            resp = sys.load(SAVE_FILE) ? jOk("{}") : jErr("载入失败");
        } else resp = jErr("未知路由");
    } else {
        httpResponse(s, 404, "Not Found", "text/plain", "404");
        return;
    }

    if (resp.empty()) resp = jErr("不支持的请求方法");
    httpResponse(s, 200, "OK", "application/json", resp);
}

// ---------------------------------------------------------------------------
// HTTP 请求读取与解析
// ---------------------------------------------------------------------------
static std::string readRequest(sock_t s) {
    std::string buf;
    char tmp[4096];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        int n = recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.append(tmp, (size_t)n);
        headerEnd = buf.find("\r\n\r\n");
        if (headerEnd == std::string::npos && buf.size() > 16384) return "";
    }
    if (headerEnd == std::string::npos) return "";
    size_t cl = getContentLength(buf.substr(0, headerEnd));
    if (cl > 1024 * 1024) return "";
    size_t bodyStart = headerEnd + 4;
    size_t have = buf.size() - bodyStart;
    while (have < cl) {
        int n = recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.append(tmp, (size_t)n);
        have += (size_t)n;
    }
    return have < cl ? "" : buf;
}

static void parseRequest(const std::string& buf, std::string& method,
                         std::string& path, std::string& query, std::string& body) {
    size_t lineEnd = buf.find("\r\n");
    std::string first = buf.substr(0, lineEnd == std::string::npos ? buf.size() : lineEnd);
    size_t s1 = first.find(' ');
    method = first.substr(0, s1);
    size_t s2 = first.find(' ', s1 + 1);
    std::string fullPath = first.substr(s1 + 1, s2 == std::string::npos ? std::string::npos : s2 - s1 - 1);
    size_t qm = fullPath.find('?');
    if (qm == std::string::npos) { path = fullPath; query = ""; }
    else { path = fullPath.substr(0, qm); query = fullPath.substr(qm + 1); }
    size_t headerEnd = buf.find("\r\n\r\n");
    size_t bodyStart = headerEnd == std::string::npos ? buf.size() : headerEnd + 4;
    size_t cl = getContentLength(headerEnd == std::string::npos ? "" : buf.substr(0, headerEnd));
    body = buf.substr(bodyStart, cl);
}

static void handleClient(sock_t s, ClubSystem& sys, AuthStore& auth, std::string client) {
    std::string buf = readRequest(s);
    if (buf.empty()) { SOCK_CLOSE_(s); return; }
    std::string method, path, query, body;
    parseRequest(buf, method, path, query, body);
    // 普通网页不能借表单向本机服务提交写入；命令行客户端可以不发送 Origin。
    std::map<std::string, std::string> requestHeaders;
    {
        std::istringstream headers(buf.substr(0, buf.find("\r\n\r\n")));
        std::string line, host, origin;
        while (std::getline(headers, line)) {
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            auto key = line.substr(0, colon);
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            auto value = line.substr(colon + 1);
            auto first = value.find_first_not_of(" \t");
            value = first == std::string::npos ? "" : value.substr(first);
            if (!value.empty() && value.back() == '\r') value.pop_back();
            if (key == "host") host = value;
            if (key == "origin") origin = value;
            if (requestHeaders.count(key)) { SOCK_CLOSE_(s); return; }
            requestHeaders[key] = value;
        }
        const std::string scheme = std::getenv("CLUB_SECURE_COOKIE") ? "https://" : "http://";
        if (method != "GET" && !origin.empty() && origin != scheme + host) {
            httpResponse(s, 403, "Forbidden", "application/json", jErr("不接受其他网站发起的写入请求"));
            SOCK_CLOSE_(s); return;
        }
    }
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        try { routeAndRespond(s, method, path, query, body, sys, auth, requestHeaders, client); }
        catch (const std::exception&) { httpResponse(s, 400, "Bad Request", "application/json", jErr("参数格式错误：请检查整数、必填字段与请求路径")); }
    }
    SOCK_CLOSE_(s);
}

// ---------------------------------------------------------------------------
// 启动服务器
// ---------------------------------------------------------------------------
static std::string getPrimaryIp() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) return "127.0.0.1";
    hostent* p = gethostbyname(hostname);
    if (!p || !p->h_addr_list[0]) return "127.0.0.1";
    in_addr addr;
    memcpy(&addr, p->h_addr_list[0], sizeof(in_addr));
    return inet_ntoa(addr);
}

static void openBrowser(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
    std::string cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
    if (system(cmd.c_str()) == 0) return;
    std::string cmd2 = "open '" + url + "' >/dev/null 2>&1 &";
    if (system(cmd2.c_str()) != 0) std::cerr << "请在浏览器中打开 " << url << '\n';
#endif
}

void startGui(ClubSystem& sys) {
    const int PORT = serverPort();
    AuthStore auth;
#ifndef _WIN32
    // A browser can cancel a request while sendAll is writing its response.
    // Treat that as a failed socket write, not a signal that kills the server.
    std::signal(SIGPIPE, SIG_IGN);
#endif
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);   // 控制台输出切到 UTF-8，避免中文横幅在 GBK 控制台乱码
    SetConsoleCP(CP_UTF8);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    // 载入已有存档
    {
        std::ifstream test(SAVE_FILE);
        if (test.good()) { test.close(); if (!sys.load(SAVE_FILE)) throw std::runtime_error("存档损坏，启动已停止；原文件未改动"); }
    }

    sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == SOCK_BAD_) { std::cerr << "socket 创建失败\n"; return; }
    int opt = 1;
#ifdef _WIN32
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(std::getenv("CLUB_LAN") ? INADDR_ANY : INADDR_LOOPBACK);
    addr.sin_port = htons((u_short)PORT);
    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "端口 " << PORT << " 被占用\n"; SOCK_CLOSE_(srv); return;
    }
    if (listen(srv, 32) < 0) { std::cerr << "listen 失败\n"; SOCK_CLOSE_(srv); return; }

    std::string ip = getPrimaryIp();
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/";
    std::cout << "\n========================================\n"
              << "  社团管理系统 · 多人共用 Web 界面\n"
              << "========================================\n"
              << "  本机访问:   " << url << "\n"
              << "  局域网访问: http://" << ip << ":" << PORT << "/\n"
              << "  (同一 Wi-Fi/局域网的人用上面这个 IP 访问)\n"
              << "  若局域网访问不通：先设置 CLUB_LAN=1；必要时手动配置防火墙\n"
              << "  netsh advfirewall firewall add rule name=ClubManager dir=in action=allow protocol=TCP localport=" << PORT << "\n"
              << "  按 Ctrl+C 退出\n"
              << "========================================\n\n";
    std::cout.flush();
    if (!auth.initialized()) std::cout << "  请先停止服务并运行 club auth init admin 初始化管理员，然后重新启动。\n";

    // 设 CLUB_NO_BROWSER=1 可跳过自动打开浏览器（用于无界面环境或测试）
    if (!getenv("CLUB_NO_BROWSER")) {
        openBrowser(url);
    }

    while (true) {
        sockaddr_in peer{};
#ifdef _WIN32
        int peerLength = sizeof(peer);
#else
        socklen_t peerLength = sizeof(peer);
#endif
        sock_t c = accept(srv, reinterpret_cast<sockaddr*>(&peer), &peerLength);
        if (c == SOCK_BAD_) continue;
#ifdef _WIN32
        DWORD timeout = 5000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
        timeval timeout{5, 0};
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
        std::thread(handleClient, c, std::ref(sys), std::ref(auth), std::string(inet_ntoa(peer.sin_addr))).detach();
    }
    SOCK_CLOSE_(srv);
#ifdef _WIN32
    WSACleanup();
#endif
}
