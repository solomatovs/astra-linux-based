#!/bin/bash
# Проверка модуля omclickhouse в собранном образе:
#   1. .so на месте, зависимости разрешаются
#   2. модуль грузится в rsyslogd и принимает все свои параметры
#   3. параметры разбирает именно модуль (чужой отвергается)
#   4. путь через libcurl рабочий: до недоступного сервера модуль не доходит
#      и говорит об этом, а не молчит
set -euo pipefail

SO=/usr/local/lib/rsyslog/omclickhouse.so
d=/tmp/chtest; rm -rf "$d"; mkdir -p "$d"

echo "== 1. artifacts =="
[ -f "$SO" ] || { echo "FAIL $SO отсутствует"; exit 1; }
echo "OK  $SO"
if ldd "$SO" | grep -q 'not found'; then
    echo "FAIL неразрешённые зависимости:"; ldd "$SO" | grep 'not found'; exit 1
fi
echo "OK  зависимости .so разрешены"

echo "== 2. load + parameters =="
cat > "$d/ok.conf" <<CONF
module(load="omclickhouse")
template(name="chInsert" type="string" option.sql="on"
    string="INSERT INTO rsyslog.SystemEvents (severity, facility, timestamp, hostname, tag, message) VALUES (%syslogseverity%, %syslogfacility%, '%timereported:::date-unixtimestamp%', '%hostname%', '%syslogtag%', '%msg%')")
*.* action(type="omclickhouse"
    server="127.0.0.1" port="8123" user="default" pwd="secret"
    template="chInsert" bulkmode="on" maxbytes="10m"
    usehttps="off" allowunsignedcerts="off" skipverifyhost="off"
    healthchecktimeout="3500" timeout="0"
    errorfile="$d/fail.log")
CONF
out=$(rsyslogd -N1 -f "$d/ok.conf" 2>&1) || { echo "FAIL конфигурация не прошла:"; echo "$out"; exit 1; }
if grep -qiE 'could not load module|unknown parameter|not known' <<<"$out"; then
    echo "FAIL модуль не загружен / параметр не принят:"; echo "$out"; exit 1
fi
echo "OK  omclickhouse загружен, параметры приняты"

echo "== 3. parameter checking =="
sed 's/bulkmode="on"/bulkmode="on" nosuchparam="1"/' "$d/ok.conf" > "$d/bad.conf"
out=$(rsyslogd -N1 -f "$d/bad.conf" 2>&1 || true)
if grep -qi 'nosuchparam' <<<"$out"; then
    echo "OK  чужой параметр отвергнут"
else
    echo "FAIL чужой параметр проглочен — параметры разбирает не модуль"; exit 1
fi

echo "== 4. curl path =="
# сервера нет: модуль обязан сходить в libcurl, получить отказ и сказать об этом
cat > "$d/dead.conf" <<CONF
global(workDirectory="$d")
module(load="imtcp")
input(type="imtcp" port="5514" address="127.0.0.1")
module(load="omclickhouse")
*.* action(type="omclickhouse" server="127.0.0.1" port="1"
    user="default" pwd="" healthchecktimeout="1000" action.resumeRetryCount="0")
CONF
rsyslogd -n -f "$d/dead.conf" -i "$d/rsyslogd.pid" > "$d/out" 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT
sleep 3
exec 3<>/dev/tcp/127.0.0.1/5514
printf '<13>%s checkhost omclickhouse-probe: hello\n' "$(date '+%b %e %H:%M:%S')" >&3
exec 3>&-
sleep 6
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
trap - EXIT

if grep -q 'omclickhouse.*Could not connect to server' "$d/out"; then
    echo "OK  модуль дошёл до libcurl и отчитался об отказе:"
    sed -n 's/^.*omclickhouse/    omclickhouse/p' "$d/out" | head -1
else
    echo "FAIL модуль не сообщил об отказе соединения:"; cat "$d/out"; exit 1
fi

echo "== OK =="
