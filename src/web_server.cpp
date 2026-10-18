// web_server.cpp
// 局域网多人共用 Web 界面：把 ClubSystem 暴露为本地 HTTP 服务器（REST API），
// 浏览器打开即图形界面。跨平台：Windows 用 Winsock，Linux 用 POSIX sockets（#ifdef 切换）。
// 简单身份选择：每个用户在顶栏选自己的成员身份，操作归属到该成员；不做密码/权限。
// 无四语切换（按需求聚焦"多人共用"）。

#include "web_server.h"
#include "system.h"

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
static const int PORT = 8090;
static const char* SAVE_FILE = "club_data.txt";

// ---------------------------------------------------------------------------
// 嵌入的前端单页（中文 UI，无四语切换）。原生 JS，无外部依赖。
// 顶栏“我的身份”下拉 = 简单身份选择：选自己的成员编号，操作归属到该成员。
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
</style>
</head>
<body>
<header>
  <span class="brand">🎭 社团管理系统</span>
  <span class="lab">多人共用 · 局域网</span>
  <span class="grow"></span>
  <span class="lab">我的身份：</span>
  <select id="me"></select>
  <button id="undo">撤销</button>
  <button id="redo">重做</button>
  <button id="save">保存</button>
</header>
<div class="layout">
  <nav id="nav"></nav>
  <main id="main"></main>
</div>
<div id="toast"></div>
<script>
const $=s=>document.querySelector(s), main=$('#main'), nav=$('#nav'), meSel=$('#me');
let ME=parseInt(localStorage.getItem('meId')||'0')||0, members=[], cur='members';
const TABS=[{k:'members',n:'成员'},{k:'clubs',n:'社团'},{k:'activities',n:'活动'},{k:'apps',n:'审批'},{k:'friends',n:'好友'},{k:'rank',n:'排行'},{k:'announce',n:'公告'}];
const esc=s=>String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
async function api(method,path,body){const o={method,headers:{}};if(body!=null){o.headers['Content-Type']='application/x-www-form-urlencoded';o.body=toForm(body);}return (await fetch(path,o)).json();}
function toForm(o){return Object.keys(o).filter(k=>o[k]!=null&&o[k]!=='').map(k=>encodeURIComponent(k)+'='+encodeURIComponent(o[k])).join('&');}
function toast(msg,ok=true){const t=$('#toast');t.textContent=msg;t.className='show '+(ok?'ok':'err');clearTimeout(t._h);t._h=setTimeout(()=>t.className='',2200);}
function buildPicker(){const act=members.filter(x=>x.active);meSel.innerHTML=act.map(m=>`<option value="${m.id}">${esc(m.name)} (#${m.id})</option>`).join('');if(!act.some(m=>m.id===ME))ME=act.length?act[0].id:0;meSel.value=ME;meSel.onchange=()=>{ME=parseInt(meSel.value);localStorage.setItem('meId',ME);};}
async function refreshMe(){const d=await api('GET','/api/members');members=d.members||[];buildPicker();}
function buildNav(){nav.innerHTML=TABS.map(t=>`<button data-tab="${t.k}">${t.n}</button>`).join('');nav.querySelectorAll('button').forEach(b=>b.onclick=()=>{cur=b.dataset.tab;render();});}
function render(){nav.querySelectorAll('button').forEach(b=>b.classList.toggle('active',b.dataset.tab===cur));({members:renderMembers,clubs:renderClubs,activities:renderActivities,apps:renderApps,friends:renderFriends,rank:renderRank,announce:renderAnnounce}[cur])();}

/* ---- 成员 ---- */
async function renderMembers(){
  const d=await api('GET','/api/members');members=d.members||[];buildPicker();
  main.innerHTML=`
  <h2>成员管理</h2>
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
  <table><thead><tr><th>ID</th><th>姓名</th><th>年级</th><th>活跃度</th><th>标签</th><th>操作</th></tr></thead><tbody>
  ${members.map(m=>`<tr style="${m.active?'':'opacity:.45'}"><td>${m.id}</td><td>${esc(m.name)}</td><td>${esc(m.grade)}</td><td>${m.score}</td>
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
async function renderClubs(){
  const c=await api('GET','/api/clubs');buildPicker();
  const clubs=c.clubs||[];
  main.innerHTML=`
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
async function renderActivities(){
  const a=await api('GET','/api/activities'),c=await api('GET','/api/clubs');buildPicker();
  const acts=a.activities||[];
  main.innerHTML=`
  <h2>活动管理</h2>
  <div class="card"><h3>创建活动</h3><div class="row">
    <input id="a_club" placeholder="社团ID" style="width:80px"><input id="a_name" placeholder="活动名称">
    <input id="a_time" placeholder="时间(展示用)" style="width:130px"><input id="a_cap" placeholder="容量" style="width:70px">
    <button class="btn" onclick="addAct(false)">创建</button></div></div>
  <div class="card"><h3>创建定时活动（带时间段+所需标签，可检测时间冲突）</h3><div class="row">
    <input id="t_club" placeholder="社团ID" style="width:80px"><input id="t_name" placeholder="名称">
    <input id="t_cap" placeholder="容量" style="width:70px"><input id="t_day" placeholder="日0-6" style="width:70px">
    <input id="t_start" placeholder="起(分)" style="width:80px"><input id="t_end" placeholder="止(分)" style="width:80px">
    <input id="t_tags" placeholder="所需标签" style="width:140px"><button class="btn" onclick="addAct(true)">创建</button></div></div>
  <table><thead><tr><th>ID</th><th>名称</th><th>社团</th><th>时间</th><th>容量</th><th>已报/候补</th><th>操作</th></tr></thead><tbody>
  ${acts.map(a=>`<tr><td>${a.id}</td><td>${esc(a.name)}</td><td>${a.clubId}</td><td>${esc(a.time)}</td><td>${a.capacity}</td>
    <td>${(a.signedUp||[]).length} / ${(a.waitlist||[]).length}</td>
    <td><button class="btn sm" onclick="signup(${a.id})">报名</button>
        <button class="btn sm gray" onclick="cancelA(${a.id})">取消</button>
        <button class="btn sm gray" onclick="setCap(${a.id})">改容量</button>
        <button class="btn sm danger" onclick="delAct(${a.id})">删除</button></td></tr>`).join('')}
  </tbody></table>`;
}
async function addAct(timed){let r;if(timed){r=await api('POST','/api/activities',{clubId:$('#t_club').value.trim(),name:$('#t_name').value.trim(),capacity:$('#t_cap').value.trim(),day:$('#t_day').value.trim(),startMin:$('#t_start').value.trim(),endMin:$('#t_end').value.trim(),requiredTags:$('#t_tags').value.trim()});}else{r=await api('POST','/api/activities',{clubId:$('#a_club').value.trim(),name:$('#a_name').value.trim(),time:$('#a_time').value.trim(),capacity:$('#a_cap').value.trim()});}toast(r.ok?('已创建 活动#'+r.data.id):(r.error||'失败'),r.ok);render();}
async function signup(aid){const r=await api('POST',`/api/activities/${aid}/signup`,{memberId:ME});toast((r.data&&r.data.message)||(r.ok?'已报名':(r.error||'失败')),r.ok);render();}
async function cancelA(aid){const r=await api('POST',`/api/activities/${aid}/cancel`,{memberId:ME});toast(r.ok?'已取消':(r.error||'失败'),r.ok);render();}
async function setCap(aid){const v=prompt('新容量:');if(v==null)return;const r=await api('POST',`/api/activities/${aid}/capacity`,{newCapacity:v});toast(r.ok?'已调整':(r.error||'失败'),r.ok);render();}
async function delAct(aid){if(!confirm('确认删除该活动?'))return;const r=await api('DELETE',`/api/activities/${aid}`);toast(r.ok?'已删除':(r.error||'失败'),r.ok);render();}

/* ---- 审批 ---- */
async function renderApps(){
  const a=await api('GET','/api/applications');buildPicker();
  const apps=a.applications||[],st={0:'待审',1:'通过',[-1]:'拒绝',[-2]:'撤回'};
  main.innerHTML=`
  <h2>入社审批（优先级队列）</h2>
  <div class="card"><h3>提交申请（以我的身份）</h3><div class="row">
    <input id="p_club" placeholder="社团ID" style="width:90px"><input id="p_prio" placeholder="优先级(0普通/1骨干/2加急)" style="width:220px">
    <button class="btn" onclick="submitApp()">提交</button>
    <button class="btn gray" onclick="processApp()">处理队首(最高优先级)</button>
    <span class="muted" id="pend"></span></div></div>
  <table><thead><tr><th>ID</th><th>成员</th><th>社团</th><th>优先级</th><th>状态</th><th>操作</th></tr></thead><tbody>
  ${apps.map(a=>`<tr><td>${a.id}</td><td>${nameOf(a.memberId)}</td><td>${a.clubId}</td><td>${a.priority}</td>
    <td>${st[a.status]||a.status}</td>
    <td><button class="btn sm" onclick="review(${a.id},true)">通过</button>
        <button class="btn sm danger" onclick="review(${a.id},false)">拒绝</button></td></tr>`).join('')}
  </tbody></table>`;
  const p=await api('GET','/api/applications/pending');$('#pend').textContent='待处理: '+(p.count||0);
}
async function submitApp(){const r=await api('POST','/api/applications',{memberId:ME,clubId:$('#p_club').value.trim(),priority:$('#p_prio').value.trim()});toast(r.ok?('已提交 申请#'+r.data.id):(r.error||'失败'),r.ok);render();}
async function processApp(){const r=await api('POST','/api/applications/process');toast(r.ok?('已处理 申请#'+r.data.id):(r.error||'无待审'),r.ok);render();}
async function review(id,ok){const r=await api('POST',`/api/applications/${id}/review`,{approve:ok});toast(r.ok?(ok?'已通过':'已拒绝'):(r.error||'失败'),r.ok);render();}

/* ---- 好友 ---- */
async function renderFriends(){buildPicker();main.innerHTML=`
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
async function renderRank(){main.innerHTML=`
  <h2>排行榜（AVL）</h2>
  <div class="card"><h3>查询</h3><div class="row">
    <input id="r_k" placeholder="Top-K" style="width:70px" value="10"><button class="btn" onclick="board()">活跃榜</button>
    <input id="r_id" placeholder="成员ID" style="width:80px" value="${ME}"><button class="btn" onclick="myRank()">个人排名</button>
    <input id="r_lo" placeholder="低" style="width:60px"><input id="r_hi" placeholder="高" style="width:60px"><button class="btn" onclick="rangeQ()">区间人数</button>
    <button class="btn gray" onclick="clubBoard()">社团活跃榜</button></div>
  <div id="rres" class="card"><span class="muted">点击上方按钮查询</span></div>`;}
async function board(){const d=await api('GET','/api/rank/leaderboard?k='+($('#r_k').value||10));$('#rres').innerHTML='<h3>活跃榜</h3>'+((d.board||[]).map((p,i)=>`${i+1}. 成员#${p[1]} — ${p[0]} 分`).join('<br>')||'空');}
async function myRank(){const d=await api('GET',`/api/rank/member/${$('#r_id').value||ME}`);$('#rres').innerHTML=`成员#${$('#r_id').value} 排名：${d.data&&d.data.rank!=null?d.data.rank:'—'}`;}
async function rangeQ(){const d=await api('GET',`/api/rank/range?lo=${$('#r_lo').value}&hi=${$('#r_hi').value}`);$('#rres').innerHTML=`活跃度区间内人数：${d.data&&d.data.count!=null?d.data.count:'—'}`;}
async function clubBoard(){const d=await api('GET','/api/rank/clubleader?k=10');$('#rres').innerHTML='<h3>社团活跃榜</h3>'+((d.board||[]).map((p,i)=>`${i+1}. 社团#${p[1]} — ${p[0]} 分`).join('<br>')||'空');}

/* ---- 公告 ---- */
async function renderAnnounce(){buildPicker();main.innerHTML=`
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
$('#save').onclick=async()=>{const r=await api('POST','/api/save');toast(r.ok?'已保存':'保存失败',r.ok);};
buildNav();refreshMe().then(render);
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
            out.push_back(s[i]);
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

static int toInt(const std::string& s, int def = 0) {
    try { return std::stoi(s); } catch (...) { return def; }
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
                         const std::string& contentType, const std::string& body) {
    std::ostringstream os;
    os << "HTTP/1.1 " << code << " " << status << "\r\n"
       << "Content-Type: " << contentType << "; charset=utf-8\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Connection: close\r\n"
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
static std::string clubJson(const Club& c) {
    std::ostringstream js;
    js << "{\"id\":" << c.id << ",\"name\":\"" << jsonEscape(c.name)
       << "\",\"category\":\"" << jsonEscape(c.category)
       << "\",\"founded\":\"" << jsonEscape(c.founded)
       << "\",\"description\":\"" << jsonEscape(c.description)
       << "\",\"parentClub\":" << c.parentClub << ",\"memberIds\":[";
    for (size_t j = 0; j < c.memberIds.size(); ++j) { if (j) js << ","; js << c.memberIds[j]; }
    js << "],\"subClubs\":[";
    for (size_t j = 0; j < c.subClubs.size(); ++j) { if (j) js << ","; js << c.subClubs[j]; }
    js << "]}";
    return js.str();
}
static std::string activityJson(const Activity& a) {
    std::ostringstream js;
    js << "{\"id\":" << a.id << ",\"clubId\":" << a.clubId
       << ",\"name\":\"" << jsonEscape(a.name)
       << "\",\"time\":\"" << jsonEscape(a.time)
       << "\",\"capacity\":" << a.capacity
       << ",\"signedUp\":[";
    for (size_t j = 0; j < a.signedUp.size(); ++j) { if (j) js << ","; js << a.signedUp[j]; }
    js << "],\"requiredTags\":[";
    for (size_t j = 0; j < a.requiredTags.size(); ++j) { if (j) js << ","; js << "\"" << jsonEscape(a.requiredTags[j]) << "\""; }
    js << "],\"day\":" << a.day << ",\"startMin\":" << a.startMin
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
static void routeAndRespond(sock_t s, const std::string& method,
                            const std::string& path, const std::string& query,
                            const std::string& body, ClubSystem& sys) {
    std::vector<std::string> seg = splitPath(path);
    Form q = parseForm(query);
    Form f = parseForm(body);
    std::string resp;
    auto saved = [&]() { sys.save(SAVE_FILE); };   // 写操作后自动存档

    // GET / -> 前端页面
    if (method == "GET" && seg.empty()) {
        httpResponse(s, 200, "OK", "text/html", kHtml);
        return;
    }

    if (!seg.empty() && seg[0] == "api") {
        // ============ 成员 ============
        if (seg.size() >= 2 && seg[1] == "members") {
            if (seg.size() == 2) {
                if (method == "GET") {
                    std::ostringstream js; js << "{\"members\":[";
                    const auto& ms = sys.members();
                    for (size_t i = 0; i < ms.size(); ++i) { if (i) js << ","; js << memberJson(ms[i]); }
                    js << "]";
                    resp = jOk(js.str());
                } else {  // POST 添加
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
                    for (size_t i = 0; i < cs.size(); ++i) { if (i) js << ","; js << clubJson(cs[i]); }
                    js << "]}"; resp = jOk(js.str());
                } else {
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
            } else if (seg.size() == 4 && seg[2] == "org" && method == "GET") {
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
                    for (size_t i = 0; i < as.size(); ++i) { if (i) js << ","; js << activityJson(as[i]); }
                    js << "]}"; resp = jOk(js.str());
                } else {
                    int clubId = toInt(formGet(f, "clubId"));
                    std::string name = formGet(f, "name");
                    int cap = toInt(formGet(f, "capacity"));
                    std::string time = formGet(f, "time");
                    std::string rtags = formGet(f, "requiredTags");
                    std::string day = formGet(f, "day"), sm = formGet(f, "startMin"), em = formGet(f, "endMin");
                    int id;
                    if (!day.empty() && !sm.empty() && !em.empty()) {
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
            } else if (seg.size() == 4 && seg[3] == "signup" && method == "POST") {
                std::string msg = sys.signup(toInt(formGet(f, "memberId")), toInt(seg[2]));
                resp = jOk("{\"message\":\"" + jsonEscape(msg) + "\"}"); saved();
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
                } else {
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
            } else if (seg.size() == 4 && seg[3] == "review" && method == "POST") {
                bool ok = sys.reviewApplication(toInt(seg[2]), formGet(f, "approve") == "true");
                resp = ok ? jOk("{}") : jErr("审批失败"); if (ok) saved();
            } else resp = jErr("未知路由");
        }
        // ============ 好友 ============
        else if (seg.size() >= 2 && seg[1] == "friends") {
            if (seg.size() == 2 && method == "POST") {
                sys.addFriend(toInt(formGet(f, "a")), toInt(formGet(f, "b")));
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
        else if (seg.size() >= 2 && seg[1] == "rank") {
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
                } else {
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
            resp = sys.undo() ? jOk("{}") : jErr("无可撤销");
        } else if (seg.size() == 2 && seg[1] == "redo" && method == "POST") {
            resp = sys.redo() ? jOk("{}") : jErr("无可重做");
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
    }
    if (headerEnd == std::string::npos) return "";
    size_t cl = getContentLength(buf.substr(0, headerEnd));
    size_t bodyStart = headerEnd + 4;
    size_t have = buf.size() - bodyStart;
    while (have < cl) {
        int n = recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.append(tmp, (size_t)n);
        have += (size_t)n;
    }
    return buf;
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

static void handleClient(sock_t s, ClubSystem& sys) {
    std::string buf = readRequest(s);
    if (buf.empty()) { SOCK_CLOSE_(s); return; }
    std::string method, path, query, body;
    parseRequest(buf, method, path, query, body);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        routeAndRespond(s, method, path, query, body, sys);
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
    system(cmd2.c_str());
#endif
}

// 首次运行尝试放行端口（一次性，用标记文件避免重复弹 UAC；失败不致命）
static void tryAddFirewallRule() {
#ifdef _WIN32
    {
        std::ifstream mark("club_fw_added.flag");
        if (mark.good()) return;   // 已处理过，不再弹 UAC
    }
    std::string params = "advfirewall firewall add rule name=\"ClubManager\" dir=in action=allow protocol=TCP localport=" +
                         std::to_string(PORT);
    std::wstring wparams(params.begin(), params.end());
    ShellExecuteW(NULL, L"runas", L"netsh", wparams.c_str(), NULL, SW_HIDE);  // 提权，弹一次 UAC
    std::ofstream("club_fw_added.flag") << "1";   // 标记已尝试，下次不再弹
#endif
}

void startGui(ClubSystem& sys) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    // 载入已有存档
    {
        std::ifstream test(SAVE_FILE);
        if (test.good()) { test.close(); sys.load(SAVE_FILE); }
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
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // 0.0.0.0：监听所有网卡，局域网可访问
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
              << "  若局域网访问不通：首次运行会弹 UAC 放行端口，或手动以管理员运行\n"
              << "  netsh advfirewall firewall add rule name=ClubManager dir=in action=allow protocol=TCP localport=" << PORT << "\n"
              << "  按 Ctrl+C 退出\n"
              << "========================================\n\n";
    std::cout.flush();

    // 设 CLUB_NO_BROWSER=1 可跳过自动开浏览器/防火墙弹窗（用于无界面环境或测试）
    if (!getenv("CLUB_NO_BROWSER")) {
        tryAddFirewallRule();
        openBrowser(url);
    }

    while (true) {
        sock_t c = accept(srv, nullptr, nullptr);
        if (c == SOCK_BAD_) continue;
        std::thread(handleClient, c, std::ref(sys)).detach();
    }
    SOCK_CLOSE_(srv);
#ifdef _WIN32
    WSACleanup();
#endif
}
