"""Real HTTP/CLI integration tests. All data lives in a temporary directory."""
import json
import http.cookiejar
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

exe = str(Path(sys.argv[1]).resolve())
checks = 0

def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)

with tempfile.TemporaryDirectory(prefix="club-http-") as directory:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    env = dict(os.environ, CLUB_NO_BROWSER="1", CLUB_PORT=str(port))
    env.pop("CLUB_LAN", None)
    env.pop("CLUB_SECURE_COOKIE", None)
    subprocess.run([exe,"auth","init","admin"],input="Test-password-2026\nTest-password-2026\n",cwd=directory,
                   text=True,encoding="utf-8",capture_output=True,check=True,timeout=15)
    opener=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
    csrf=""
    process = subprocess.Popen([exe, "gui"], cwd=directory, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    base = f"http://127.0.0.1:{port}"

    def request(path, method="GET", data=None, raw=False):
        body = urllib.parse.urlencode(data or {}).encode() if method != "GET" else None
        req = urllib.request.Request(base+path, data=body, method=method)
        if method != "GET" and csrf: req.add_header("X-CSRF-Token",csrf)
        try:
            response = opener.open(req, timeout=15)
        except urllib.error.HTTPError as error:
            response = error
        value = response.read().decode("utf-8")
        return value if raw else json.loads(value)

    def post(path, **data):
        return request(path, "POST", data)

    try:
        deadline = time.monotonic()+12
        while True:
            try:
                request("/api/auth/status")
                break
            except (OSError, ValueError):
                if process.poll() is not None or time.monotonic()>deadline:
                    raise RuntimeError("Server failed to start")
                time.sleep(.1)
        csrf=post("/api/auth/login",username="admin",password="Test-password-2026")["data"]["csrf"]
        check("我的日程" in request("/", raw=True), "HTML contains schedule")
        check(request("/api/members")["data"]["members"] == [], "valid empty members JSON")
        a=post("/api/members", name="Alice Test", grade="2026", tags="Python")["data"]["id"]
        b=post("/api/members", name="Bob", grade="2026", tags="Python")["data"]["id"]
        check(a==0 and b==1, "member creation")
        check(request("/api/members")["data"]["members"][0]["name"]=="Alice Test", "form plus decoding")
        check(not post("/api/members", name=" ")["ok"], "empty name rejected")
        cl=post("/api/clubs", name="Club")["data"]["id"]
        child=post("/api/clubs", name="Child", parentId=cl)["data"]["id"]
        check(len(request("/api/clubs/org")["data"]["lines"])==2,"org route")
        check(request(f"/api/clubs/{child}","DELETE")["ok"],"remove child")
        check(len(request("/api/clubs")["data"]["clubs"])==1,"hide removed clubs")
        act=post("/api/activities", clubId=cl, name="Weekly", capacity=1, day=2, startMin=600,endMin=660,requiredTags="Python")["data"]["id"]
        check(post(f"/api/activities/{act}/signup",memberId=a)["ok"],"signup")
        check(post(f"/api/activities/{act}/signup",memberId=b)["ok"],"waitlist")
        check(len(request("/api/activities")["data"]["activities"][0]["waitlist"])==1,"serialize waitlist")
        check(not post(f"/api/activities/{act}/signup",memberId=a)["ok"],"duplicate signup failure")
        check(post(f"/api/activities/{act}/cancel",memberId=a)["ok"],"cancel with promotion")
        check(post("/api/undo")["ok"],"undo cancellation")
        check(request("/api/activities")["data"]["activities"][0]["signedUp"]==[a],"undo exact roster")
        check(post(f"/api/activities/{act}/checkin",memberId=a)["ok"],"checkin")
        check(not post(f"/api/activities/{act}/checkin",memberId=a)["ok"],"checkin idempotency")
        check(request("/api/stats")["data"]["totalScore"]==10,"checkin points")
        check(len(request(f"/api/schedule/{a}")["data"]["activities"])==1,"schedule")
        check("已签到" in request(f"/api/activities/{act}/export",raw=True),"roster CSV")
        check("Alice Test" in request("/api/export/members",raw=True),"members CSV")
        app=post("/api/applications",memberId=b,clubId=cl,priority=3)["data"]["id"]
        check(not post("/api/applications",memberId=b,clubId=cl)["ok"],"duplicate application")
        check(post("/api/applications/process")["data"]["id"]==app,"next application")
        check(request("/api/applications/pending")["data"]["count"]==1,"next does not lose pending item")
        check(not post(f"/api/applications/{app}/withdraw",memberId=a)["ok"],"withdraw requires matching member")
        check(post(f"/api/applications/{app}/withdraw",memberId=b)["ok"],"withdraw")
        check(post("/api/undo")["ok"],"undo withdrawal")
        check(post(f"/api/applications/{app}/review",approve="true")["ok"],"review")
        check(post("/api/undo")["ok"],"undo review")
        check(request("/api/clubs")["data"]["clubs"][0]["memberIds"]==[],"review undo membership")
        for path,method,data in [("/api/rank","GET",None),("/api/members/nope","DELETE",None),
                                 ("/api/members/0/score","POST",{"delta":"10oops"}),
                                 ("/api/activities","POST",{"clubId":cl,"name":"bad","capacity":2,"day":3}),
                                 ("/api/members","PUT",{"name":"unexpected"})]:
            check(not request(path,method,data)["ok"],f"invalid request {path}")
        check(request("/api/stats")["data"]["totalScore"]==10,"invalid requests preserve score")
        foreign = urllib.request.Request(base+"/api/undo",data=b"",headers={"Origin":"https://example.invalid"},method="POST")
        try:
            urllib.request.urlopen(foreign,timeout=5)
            check(False,"cross-origin mutation must fail")
        except urllib.error.HTTPError as error:
            check(error.code==403,"cross-origin mutation rejected")
        check(post(f"/api/activities/{act}/close")["ok"],"close activity")
        check(not post(f"/api/activities/{act}/signup",memberId=b)["ok"],"closed registration rejected")
        check(post("/api/load")["ok"],"reload saved state")
        check(request("/api/activities")["data"]["activities"][0]["closed"],"closed persisted")
        check(request("/api/stats")["data"]["checkIns"]==1,"checkin persisted")
        check(post("/api/import/members",csv='姓名,年级,标签\n"Comma, Name",2026,Go\n')["data"]["added"]==1,"CSV import")
        check(post("/api/undo")["ok"],"undo import")
        check(post("/api/load")["ok"],"reload after undo")
        check(request("/api/stats")["data"]["members"]==2,"undo auto saved")
    finally:
        process.terminate()
        process.wait(timeout=8)
        process.stderr.close()

    def cli(*args):
        return subprocess.run([exe,*args],cwd=directory,env=env,capture_output=True,encoding="utf-8",timeout=10)
    check(cli("stats").returncode==0,"CLI stats")
    check("Weekly" in cli("schedule",str(a)).stdout,"CLI schedule")
    check(cli("export","roster",str(act),"roster.csv").returncode==0,"CLI export")
    check(Path(directory,"roster.csv").exists(),"export file exists")
    sample=Path(__file__).resolve().parent.parent/"examples/sample_data.txt"
    check(cli("load",str(sample)).returncode==0,"legacy sample load")
    check("成员=8" in cli("stats").stdout,"legacy sample counters")
    archive=Path(directory,"club_data.txt")
    archive.write_text("broken save",encoding="utf-8")
    check(cli("member","add","MustNotWrite","2026").returncode!=0,"corrupt save stops CLI")
    check(archive.read_text(encoding="utf-8")=="broken save","corrupt archive preserved")
    check(cli("load",str(sample)).returncode==0,"recover by explicit import")

print(f"HTTP/CLI TESTS PASSED ({checks} checks)")
