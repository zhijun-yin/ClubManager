"""Authorization tests against real sockets. Temporary credentials/data only."""
import hashlib
import http.cookiejar
import json
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

EXE = str(Path(sys.argv[1]).resolve())
PASSWORD = "Test-password-2026"
checks = 0

def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)

class Client:
    def __init__(self, base):
        self.base = base
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar))
        self.csrf = ""

    def request(self, path, method="GET", data=None, headers=None):
        body = urllib.parse.urlencode(data or {}).encode() if method != "GET" else None
        h = {"X-CSRF-Token": self.csrf} if self.csrf and method != "GET" else {}
        h.update(headers or {})
        req = urllib.request.Request(self.base+path, data=body, method=method, headers=h)
        try:
            response = self.opener.open(req, timeout=20)
        except urllib.error.HTTPError as error:
            response = error
        raw = response.read().decode("utf-8")
        value = json.loads(raw) if "application/json" in response.headers.get("Content-Type", "") else raw
        return response.status, value, response.headers

    def get(self, path):
        status, value, _ = self.request(path)
        check(status == 200 and value["ok"], "GET " + path)
        return value["data"]

    def post(self, path, **data):
        status, value, _ = self.request(path, "POST", data)
        check(status == 200 and value["ok"], "POST " + path + ": " + str(value))
        return value["data"]

    def login(self, username, password=PASSWORD):
        result = self.post("/api/auth/login", username=username, password=password)
        self.csrf = result["csrf"]
        return result

    def denied(self, path, method="POST", data=None, code=403, headers=None):
        status, result, _ = self.request(path, method, data, headers)
        check(status == code and not result["ok"], f"deny {method} {path}: {status} {result}")

with tempfile.TemporaryDirectory(prefix="club-auth-") as directory:
    root = Path(directory)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    env = dict(os.environ, CLUB_NO_BROWSER="1", CLUB_PORT=str(port))
    env.pop("CLUB_LAN", None)
    env.pop("CLUB_SECURE_COOKIE", None)
    base = f"http://127.0.0.1:{port}"
    process = None

    def cli(*args, password=None):
        return subprocess.run([EXE,*args],cwd=directory,env=env,input=password,capture_output=True,
                              text=True,encoding="utf-8",timeout=20)

    def stop():
        global process
        if process is not None:
            process.terminate()
            process.wait(timeout=10)
            process = None

    def start():
        global process
        process = subprocess.Popen([EXE,"gui"],cwd=directory,env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            try:
                Client(base).request("/api/auth/status")
                return
            except OSError:
                if process.poll() is not None: raise RuntimeError("server failed to start")
                time.sleep(.1)
        raise RuntimeError("server readiness timeout")

    try:
        start()
        anonymous=Client(base)
        check(not anonymous.get("/api/auth/status")["initialized"],"uninitialized status")
        anonymous.denied("/api/auth/login",data={"username":"admin","password":PASSWORD})
        anonymous.denied("/api/auth/init",data={"username":"admin","password":PASSWORD},code=401)
        for path in ["/api/members","/api/activities","/api/accounts","/api/export/members","/api/stats","/club_accounts.txt","/club_data.txt"]:
            anonymous.denied(path,"GET",code=401)
        stop()
        check(cli("auth","init","admin",password="short\nshort\n").returncode!=0,"short bootstrap password rejected")
        check(cli("auth","init","admin",password=PASSWORD+"\n"+PASSWORD+"\n").returncode==0,"local bootstrap")
        original_credentials=(root/"club_accounts.txt").read_bytes()
        check(cli("auth","init","other",password=PASSWORD+"\n"+PASSWORD+"\n").returncode!=0,"cannot reinitialize")
        check((root/"club_accounts.txt").read_bytes()==original_credentials,"failed init preserved credentials")
        # A Windows-edited credential archive must also load on Linux.
        (root/"club_accounts.txt").write_bytes(original_credentials.replace(b"\r\n",b"\n").replace(b"\n",b"\r\n"))
        start()
        admin=Client(base)
        admin.denied("/api/auth/login",data={"username":"admin","password":"wrong"},code=401)
        status, result, headers=admin.request("/api/auth/login","POST",{"username":"admin","password":PASSWORD})
        check(status==200 and result["data"]["user"]["role"]=="admin","admin role from server")
        admin.csrf=result["data"]["csrf"]
        cookie=headers.get("Set-Cookie","")
        check("HttpOnly" in cookie and "SameSite=Strict" in cookie and "Max-Age=28800" in cookie,"cookie protections")
        check(len(admin.csrf)==64,"random CSRF token")
        admin.denied("/api/members",data={"name":"NoCSRF"},headers={"X-CSRF-Token":""})
        admin.denied("/api/members",data={"name":"ForgedCSRF"},headers={"X-CSRF-Token":"0"*64})
        admin.denied("/api/members",data={"name":"Origin"},headers={"Origin":"https://evil.invalid"})
        alice=admin.post("/api/members",name="Alice",grade="2026")["id"]
        bob=admin.post("/api/members",name="Bob",grade="2026")["id"]
        spare=admin.post("/api/members",name="Spare",grade="2026")["id"]
        club=admin.post("/api/clubs",name="Reading Club")["id"]
        act=admin.post("/api/activities",clubId=club,name="Weekly",capacity=2)["id"]
        waiting=admin.post("/api/activities",clubId=club,name="Waitlist",capacity=1)["id"]
        for username,mid in [("alice",alice),("bobby",bob)]:
            admin.post("/api/accounts",username=username,password=PASSWORD,role="member",memberId=mid)
        admin.post("/api/accounts",username="backup",password=PASSWORD,role="admin")
        for values in [dict(username="bad|name",role="admin"),dict(username="shortpass",role="admin",password="short"),
                       dict(username="rolebad",role="owner"),dict(username="unbound",role="member"),
                       dict(username="missing",role="member",memberId=99),dict(username="duplicate",role="member",memberId=alice)]:
            admin.denied("/api/accounts",data={"password":PASSWORD,**values},code=400)
        accounts=admin.get("/api/accounts")["accounts"]
        check(len(accounts)==4 and all("hash" not in a and "salt" not in a and "memberKey" not in a for a in accounts),"no credentials in API")
        raw=(root/"club_accounts.txt").read_text(encoding="utf-8")
        check(PASSWORD not in raw,"no plaintext password on disk")
        salts=[]
        for line in raw.splitlines()[1:-1]:
            name,role,mid,key,enabled,salt,digest=line.split("|")
            expected=hashlib.pbkdf2_hmac("sha256",PASSWORD.encode(),bytes.fromhex(salt),600000).hex()
            check(digest==expected,"platform KDF matches independent Python implementation")
            salts.append(salt)
        check(len(set(salts))==len(salts),"unique per-account salts")
        member=Client(base); member.login("alice")
        other=Client(base); other.login("bobby")
        check(member.get("/api/auth/status")["user"]["memberId"]==alice,"bound member identity")
        own=member.get("/api/members")["members"]
        check(len(own)==1 and own[0]["id"]==alice and "accountKey" not in own[0],"own profile only")
        denied_reads=["/api/stats","/api/accounts","/api/state","/api/export/members",f"/api/activities/{act}/export",
                      f"/api/activities/{act}/waitlist",f"/api/schedule/{bob}","/api/members/bytag?tag=x",
                      "/api/clubs/org","/api/announcements",f"/api/announcements/member/{bob}","/api/friends/0/recommend",
                      "/api/rank/leaderboard","/api/anything/new"]
        for path in denied_reads: member.denied(path,"GET")
        denied_writes=[("/api/members",{}),(f"/api/members/{alice}/score",{"delta":999}),
                       (f"/api/members/{alice}/tag",{"tag":"admin"}), ("/api/clubs",{"name":"Nope"}),
                       (f"/api/clubs/{club}/join",{"memberId":alice}), (f"/api/clubs/{club}/leave",{"memberId":alice}),
                       ("/api/activities",{"clubId":club}), (f"/api/activities/{act}/capacity",{"capacity":99}),
                       (f"/api/activities/{act}/close",{}),("/api/import/members",{"csv":"Hacker,2026"}),
                       ("/api/applications/process",{}),("/api/friends",{"a":alice,"b":bob}),
                       ("/api/announcements",{"clubId":club,"message":"hi","senderId":alice}),
                       ("/api/undo",{}),("/api/redo",{}),("/api/save",{}),("/api/load",{}),
                       ("/api/accounts",{"username":"hacker","role":"admin","password":PASSWORD}),
                       ("/api/accounts/alice",{"role":"admin"}),("/api/accounts/admin/password",{"password":PASSWORD})]
        for path,data in denied_writes: member.denied(path,data=data)
        for path in [f"/api/members/{bob}",f"/api/clubs/{club}",f"/api/activities/{act}"]:
            member.denied(path,"DELETE")
        for action in ["signup","cancel","checkin"]:
            member.denied(f"/api/activities/{act}/{action}",data={"memberId":bob})
        member.denied("/api/applications",data={"clubId":club,"memberId":bob})
        member.denied("/api/applications",data={"clubId":club,"priority":9})
        # Duplicate parameters must not allow parser disagreement to bypass checks.
        req=urllib.request.Request(base+f"/api/activities/{act}/signup",data=b"memberId=0&memberId=1",method="POST",headers={"X-CSRF-Token":member.csrf})
        try:
            member.opener.open(req,timeout=10)
            check(False,"duplicate identity rejected")
        except urllib.error.HTTPError as error:
            check(error.code==400,"duplicate identity rejected")
        member.post(f"/api/activities/{act}/signup")
        other.post(f"/api/activities/{act}/signup")
        roster=member.get("/api/activities")["activities"][0]
        check(roster["signedUp"]==[alice] and roster["signedCount"]==2,"aggregate capacity without other identities")
        member.post(f"/api/activities/{act}/cancel",memberId=alice)
        member.post(f"/api/activities/{act}/signup",memberId=alice)
        member.post(f"/api/activities/{act}/checkin")
        _,repeat,_=member.request(f"/api/activities/{act}/checkin","POST")
        check(not repeat["ok"],"cannot award repeated points")
        check(member.get("/api/members")["members"][0]["score"]==10,"own check-in score")
        other.post(f"/api/activities/{waiting}/signup")
        member.post(f"/api/activities/{waiting}/signup")
        check(member.get("/api/activities")["activities"][1]["waitlist"]==[alice],"member waitlist signup success")
        member.post(f"/api/activities/{waiting}/cancel")
        p1=member.post("/api/applications",clubId=club)["id"]
        p2=other.post("/api/applications",clubId=club)["id"]
        check([a["id"] for a in member.get("/api/applications")["applications"]]==[p1],"only own applications")
        check(member.get("/api/applications/pending")["count"]==1,"own pending count")
        member.denied(f"/api/applications/{p2}/withdraw")
        member.denied(f"/api/applications/{p1}/review",data={"approve":"true"})
        member.post(f"/api/applications/{p1}/withdraw")
        check(member.get("/api/applications")["applications"][0]["status"]==-2,"withdraw persisted")
        p3=member.post("/api/applications",clubId=club)["id"]
        admin.post(f"/api/applications/{p3}/review",approve="true")
        admin.post(f"/api/clubs/{club}/join",memberId=bob)
        check(member.get("/api/clubs")["clubs"][0]["memberIds"]==[alice],"only own club membership identity")
        admin.post("/api/announcements",clubId=club,senderId=bob,message="Welcome")
        check(member.get(f"/api/announcements/member/{alice}")["announcements"][0]["message"]=="Welcome","own announcements")
        # Role/enable/password changes invalidate already-issued sessions.
        admin.post("/api/accounts/alice",role="member",memberId=alice,enabled="false")
        member.denied("/api/members","GET",code=401)
        member.denied("/api/auth/login",data={"username":"alice","password":PASSWORD},code=401)
        admin.post("/api/accounts/alice",role="member",memberId=alice,enabled="true")
        member.login("alice")
        admin.post("/api/accounts/alice",role="admin",memberId=alice)
        member.denied("/api/members","GET",code=401)
        member.login("alice")
        check("members" in member.get("/api/stats"),"promoted admin permissions")
        admin.post("/api/accounts/alice",role="member",memberId=alice)
        member.denied("/api/stats","GET",code=401)
        member.login("alice")
        member.denied("/api/stats","GET")
        replacement=Client(base); replacement.login("alice")
        member.denied("/api/members","GET",code=401)
        member=replacement
        admin.post("/api/accounts/alice/password",password="Replacement-2026")
        member.denied("/api/members","GET",code=401)
        member.denied("/api/auth/login",data={"username":"alice","password":PASSWORD},code=401)
        member.login("alice","Replacement-2026")
        member.denied("/api/auth/password",data={"currentPassword":"wrong","password":PASSWORD},code=400)
        member.post("/api/auth/password",currentPassword="Replacement-2026",password=PASSWORD)
        member.denied("/api/members","GET",code=401)
        member.login("alice")
        member.post("/api/auth/logout")
        member.denied("/api/members","GET",code=401)
        admin.post("/api/accounts/backup",role="admin",enabled="false")
        admin.denied("/api/accounts/admin",data={"role":"admin","enabled":"false"},code=400)
        admin.denied("/api/accounts/admin",data={"role":"member","memberId":spare},code=400)
        check(admin.get("/api/auth/status")["authenticated"],"last admin stays usable")
        member.login("alice")
        stop(); start()
        member.denied("/api/members","GET",code=401)
        admin.login("admin"); member.login("alice")
        check(member.get("/api/members")["members"][0]["score"]==10,"account binding and state survive restart")
        # Loading a different dataset with the same numeric IDs must not transfer identity.
        original_data=(root/"club_data.txt").read_bytes()
        sample=Path(__file__).resolve().parent.parent/"examples/sample_data.txt"
        (root/"club_data.txt").write_bytes(sample.read_bytes())
        admin.post("/api/load")
        member.denied("/api/members","GET",code=401)
        member.denied("/api/auth/login",data={"username":"alice","password":PASSWORD},code=401)
        check(not next(a for a in admin.get("/api/accounts")["accounts"] if a["username"]=="alice")["bindingValid"],"invalid binding flagged")
        (root/"club_data.txt").write_bytes(original_data)
        admin.post("/api/load"); member.login("alice")
        # Member removal disables access immediately, even with a valid session.
        admin.post("/api/accounts",username="spareuser",role="member",memberId=spare,password=PASSWORD)
        spare_client=Client(base); spare_client.login("spareuser")
        status,value,_=admin.request(f"/api/members/{spare}","DELETE")
        check(status==200 and value["ok"],"remove bound member")
        spare_client.denied("/api/members","GET",code=401)
        admin.post("/api/undo"); spare_client.login("spareuser")
        # Creating an account is outside business undo history. Undo/redo of
        # the preceding member creation must preserve its later account binding.
        redo_member=admin.post("/api/members",name="Redo member")["id"]
        admin.post("/api/accounts",username="redouser",role="member",memberId=redo_member,password=PASSWORD)
        redo_client=Client(base); redo_client.login("redouser")
        admin.post("/api/undo")
        redo_client.denied("/api/members","GET",code=401)
        admin.post("/api/redo"); redo_client.login("redouser")
        check(redo_client.get("/api/members")["members"][0]["name"]=="Redo member","account survives member creation redo")
        # Login throttling uses actual socket peer, not caller-controlled proxy headers.
        bad=Client(base)
        for i in range(5):
            bad.denied("/api/auth/login",data={"username":"absent","password":"wrong"},code=401,headers={"X-Forwarded-For":f"10.0.0.{i}"})
        bad.denied("/api/auth/login",data={"username":"admin","password":PASSWORD},code=429)
        stop()
        check(cli("auth","reset","admin",password="Recovered-2026\nRecovered-2026\n").returncode==0,"local recovery")
        start(); admin.login("admin","Recovered-2026"); stop()
        (root/"club_accounts.txt").write_text("corrupt credentials",encoding="utf-8")
        broken=cli("gui")
        check(broken.returncode!=0,"corrupt account store fails closed")
        check((root/"club_accounts.txt").read_text(encoding="utf-8")=="corrupt credentials","no credential overwrite on corruption")
    finally:
        stop()

print(f"AUTHORIZATION TESTS PASSED ({checks} checks)")
