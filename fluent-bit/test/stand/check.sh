#!/bin/bash
# Проверка стенда: оба плагина отдают метрики, метки на месте, режим both
# развёл метрики и записи по разным тегам, курсор переживает перезапуск.
#
# Стенд должен быть уже поднят: docker compose -f test/stand/docker-compose.yml up -d
set -u

cd "$(dirname "$0")"
DC=(docker compose -f docker-compose.yml)
URL=${URL:-http://127.0.0.1:2021/metrics}
WAIT=${WAIT:-60}
SEED=${SEED:-1}
fails=0

check() {  # $1=что ищем $2=пояснение
    if grep -q "$1" <<<"$body"; then
        echo "OK   $2"
    else
        echo "FAIL $2 (нет '$1')" >&2
        fails=$((fails + 1))
    fi
}

fetch() {
    for _ in $(seq 1 "$WAIT"); do
        body=$(curl -sf -m 10 "$URL" 2>/dev/null) && [ -n "$body" ] && return 0
        sleep 1
    done
    return 1
}

# на пустых серверах половина системных таблиц пуста, а system.part_log ещё
# и не создана — без данных проверять нечего
if [ "$SEED" = 1 ]; then
    ./seed.sh >/dev/null || { echo "FAIL наполнение стенда не прошло" >&2; exit 1; }
    sleep 10
fi

echo "== метрики $URL =="
if ! fetch; then
    echo "FAIL экспортёр молчит через $WAIT с" >&2
    exit 1
fi

# postgres: метрика без меток, счётчик с меткой из столбца, свёртка, Value_Fields
check 'pg_server_version_num{'   'pg: метрика без меток'
check 'pg_database_xact_commit{' 'pg: счётчик по базам'
check 'database="postgres"'      'pg: метка из столбца database'
check 'pg_waits_backends{'       'pg: свёрнутый профиль ожиданий'
check 'pg_table_n_live_tup{'     'pg: таблицы с Value_Fields'
check 'table="demo_events"'      'pg: метка из столбца table'

# столбцы вне Value_Fields не должны стать метриками
if grep -q 'pg_table_vacuum_count{' <<<"$body"; then
    echo "FAIL pg: Value_Fields не отсёк лишние столбцы" >&2
    fails=$((fails + 1))
else
    echo "OK   pg: Value_Fields отсёк лишние столбцы"
fi

# clickhouse: показатели ноды, метка из столбца, режим both
check 'clickhouse_inserted_rows{' 'ch: показатели ноды из metric_log'
check 'clickhouse_async_value{'   'ch: asynchronous_metrics с меткой metric'
check 'clickhouse_parts_parts{'   'ch: режим both отдал метрики под своим тегом'
check 'table="events"'            'ch: метка из столбца table'

# instance у clickhouse приходит из hostName() самой ноды, а не из targets
if grep -qE 'clickhouse_inserted_rows\{instance="[^"]+"' <<<"$body"; then
    echo "OK   ch: instance взят из hostName()"
else
    echo "FAIL ch: метки instance нет" >&2
    fails=$((fails + 1))
fi

# записи режима both идут в stdout, а не в экспортёр
logs=$("${DC[@]}" logs fluent-bit 2>&1)
if grep -q '"bytes_on_disk"' <<<"$logs"; then
    echo "OK   ch: режим both отдал записи в stdout"
else
    echo "FAIL ch: записей режима both в stdout нет" >&2
    fails=$((fails + 1))
fi

# насос pg2ch: строки из PG доехали в CH без потерь и дублей
pg_rows=$("${DC[@]}" exec -T -e PGPASSWORD=fluentbit postgres \
    psql -U postgres -d postgres -Atc "SELECT count(*) FROM demo_events" 2>/dev/null)
for _ in $(seq 1 12); do
    ch_rows=$("${DC[@]}" exec -T clickhouse clickhouse client \
        --query "SELECT count() FROM demo.pg_events" 2>/dev/null)
    [ "$ch_rows" = "$pg_rows" ] && break
    sleep 2
done
if [ "${ch_rows:-0}" = "$pg_rows" ] && [ -n "$pg_rows" ] && [ "$pg_rows" -gt 0 ]; then
    echo "OK   pg2ch: перегнано строк $ch_rows из $pg_rows"
else
    echo "FAIL pg2ch: в CH ${ch_rows:-0} строк, в PG $pg_rows" >&2
    fails=$((fails + 1))
fi
dups=$("${DC[@]}" exec -T clickhouse clickhouse client \
    --query "SELECT count() - uniqExact(id) FROM demo.pg_events" 2>/dev/null)
if [ "${dups:-1}" = "0" ]; then
    echo "OK   pg2ch: дублей нет"
else
    echo "FAIL pg2ch: дублей $dups" >&2
    fails=$((fails + 1))
fi
if "${DC[@]}" logs fluent-bit 2>&1 | grep -q '"table":"pg_events","status":"ok"'; then
    echo "OK   pg2ch: отчёт о прогоне в конвейере"
else
    echo "FAIL pg2ch: отчёта о прогоне нет" >&2
    fails=$((fails + 1))
fi

# выходные плагины: агрегация SQL на вставке в CH и метрики в PG
ch_q() { "${DC[@]}" exec -T clickhouse clickhouse client --query "$1" 2>/dev/null; }
pg_q() { "${DC[@]}" exec -T -e PGPASSWORD=fluentbit postgres \
             psql -U postgres -d postgres -Atc "$1" 2>/dev/null; }

agg=$(ch_q "SELECT count() FROM demo.nginx_agg")
if [ "${agg:-0}" -gt 0 ]; then
    echo "OK   out_clickhouse: агрегат на вставке ($agg строк вместо сырых)"
else
    echo "FAIL out_clickhouse: в demo.nginx_agg пусто" >&2
    fails=$((fails + 1))
fi

hosts=$(ch_q "SELECT count(DISTINCT host) FROM demo.nginx_agg")
if [ "${hosts:-0}" = "2" ]; then
    echo "OK   out_clickhouse: обе метки хостов доехали"
else
    echo "FAIL out_clickhouse: хостов $hosts вместо 2" >&2
    fails=$((fails + 1))
fi

mrows=$(pg_q "SELECT count(*) FROM flb_metrics")
mnames=$(pg_q "SELECT count(DISTINCT name) FROM flb_metrics")
if [ "${mrows:-0}" -gt 0 ] && [ "${mnames:-0}" -gt 1 ]; then
    echo "OK   out_postgres: метрик $mnames видов, строк $mrows"
else
    echo "FAIL out_postgres: в flb_metrics строк ${mrows:-0}" >&2
    fails=$((fails + 1))
fi

if [ "$(pg_q "SELECT count(*) FROM flb_metrics WHERE labels ? 'hostname'")" -gt 0 ]; then
    echo "OK   out_postgres: метки метрик приехали в jsonb"
else
    echo "FAIL out_postgres: меток в jsonb нет" >&2
    fails=$((fails + 1))
fi

# кольцевой буфер: накопление, вытеснение старого, очистка при выдаче
ring_stats() { curl -sf -m 5 "http://127.0.0.1:2022/stats" 2>/dev/null; }
for _ in $(seq 1 15); do
    [ -n "$(ring_stats)" ] && break
    sleep 2
done
st=$(ring_stats)
if [ -n "$st" ] && [ "$(sed -E 's/.*"records":([0-9]+).*/\1/' <<<"$st")" -gt 0 ]; then
    echo "OK   ring: записи копятся ($st)"
else
    echo "FAIL ring: кольцо пусто или недоступно" >&2
    fails=$((fails + 1))
fi

# кольцо 16K при потоке 25 записей/с должно вытеснять старое
if [ "$(sed -E 's/.*"dropped":([0-9]+).*/\1/' <<<"$st")" -gt 0 ]; then
    echo "OK   ring: старое вытесняется по кругу"
else
    echo "WARN ring: вытеснения пока не было — кольцо не переполнилось" >&2
fi

n1=$(curl -sf -m 10 http://127.0.0.1:2022/logs | wc -l)
n2=$(curl -sf -m 10 http://127.0.0.1:2022/logs/clear | wc -l)
after=$(sed -E 's/.*"records":([0-9]+).*/\1/' <<<"$(ring_stats)")
if [ "$n1" -gt 0 ] && [ "$n2" -gt 0 ] && [ "${after:-1}" -lt "$n2" ]; then
    echo "OK   ring: GET отдал ($n1), GET /clear отдал ($n2) и очистил"
else
    echo "FAIL ring: выдача $n1/$n2, после очистки осталось ${after:-?}" >&2
    fails=$((fails + 1))
fi

# одновременные запросы не должны отдать одно и то же дважды
sleep 3
rm -f /tmp/ring-par-*.txt
for i in 1 2 3 4 5; do curl -sf -m 10 http://127.0.0.1:2022/logs/clear > /tmp/ring-par-$i.txt & done
wait
nonempty=$(grep -lc . /tmp/ring-par-*.txt 2>/dev/null | wc -l)
if [ "$nonempty" -le 1 ]; then
    echo "OK   ring: одновременные запросы сериализованы, дублей нет"
else
    echo "FAIL ring: $nonempty из 5 запросов получили данные одновременно" >&2
    fails=$((fails + 1))
fi
rm -f /tmp/ring-par-*.txt

# пробы: состояния портов, icmp и http
check 'probe_success{'    'probe: probe_success есть (имена как у blackbox)'
check 'probe_port_state{' 'probe: состояние порта отдельной метрикой'
check 'type="icmp"'       'probe: icmp отработал'
check 'type="http"'       'probe: http отработал'

check 'probe_ssl_earliest_cert_expiry{' 'probe: срок сертификата unix-временем'
check 'probe_ssl_cert_expiry_days{'     'probe: срок сертификата в днях'

# дата истечения: метрика, дни и строка в записи должны сходиться между собой
# и с самим сертификатом
exp_unix=$(grep -m1 -oE 'probe_ssl_earliest_cert_expiry\{[^}]*\} [0-9]+' <<<"$body" | awk '{print $2}')
# запись пробы уходит в stdout по Match log.*, кольцо ловит только sink.nginx
# именно последнюю: первые пробы сделаны до того, как seed.sh включил ssl,
# и у них cert_not_after законно пуст
rec=$("${DC[@]}" logs fluent-bit 2>&1 | grep '"type":"tls"' | tail -1)
rec_date=$(sed -n 's/.*"cert_not_after":"\([^"]*\)".*/\1/p' <<<"$rec")
real=$("${DC[@]}" exec -T -u 0 postgres \
    openssl x509 -in /var/lib/postgresql/data/server.crt -noout -enddate 2>/dev/null |
    sed 's/notAfter=//')
real_unix=$(date -u -d "$real" +%s 2>/dev/null || echo 0)
metric_date=$(date -u -d "@${exp_unix:-0}" '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || echo -)
if [ -n "$rec_date" ] && [ "$rec_date" = "$metric_date" ] &&
   [ -n "$real_unix" ] && [ "$real_unix" -eq "${exp_unix:-0}" ] 2>/dev/null; then
    echo "OK   probe: дата истечения сходится везде ($rec_date)"
else
    echo "FAIL probe: дата истечения расходится: метрика=$metric_date запись=$rec_date сертификат=$real" >&2
    fails=$((fails + 1))
fi

open_ports=$(grep -c 'probe_port_state{[^}]*} 1' <<<"$body")
closed_ports=$(grep -c 'probe_port_state{[^}]*} 0' <<<"$body")
if [ "$open_ports" -ge 2 ] && [ "$closed_ports" -ge 1 ]; then
    echo "OK   probe: открытых портов $open_ports, закрытых $closed_ports"
else
    echo "FAIL probe: открытых $open_ports, закрытых $closed_ports (ждали >=2 и >=1)" >&2
    fails=$((fails + 1))
fi

# журнал с курсором. Сравнивать количество нельзя: насос pg2ch продолжает
# вставлять и рождать новые куски — событий законно прибывает. Перечитанный
# журнал выдаёт себя иначе: одно и то же событие приезжает дважды, а благодаря
# Time_Field повтор совпадает как строка целиком
# снимок логов берётся заново: с момента предыдущего прошли проверки выходов,
# и за это время журнал успел наполниться
for _ in $(seq 1 10); do
    logs=$("${DC[@]}" logs fluent-bit 2>&1)
    grep -q '"event_type"' <<<"$logs" && break
    sleep 2
done

if grep -q '"event_type"' <<<"$logs"; then
    echo "OK   ch: журнал part_log прочитан"
    "${DC[@]}" restart fluent-bit >/dev/null 2>&1
    sleep 12
    dup_events=$("${DC[@]}" logs fluent-bit 2>&1 | grep '"event_type"' | sort | uniq -d | wc -l)
    if [ "$dup_events" = "0" ]; then
        echo "OK   ch: курсор пережил перезапуск, повторов событий нет"
    else
        echo "FAIL ch: после перезапуска $dup_events повторно прочитанных событий" >&2
        fails=$((fails + 1))
    fi
else
    echo "WARN ch: событий в part_log не было, курсор не проверен" >&2
fi

echo
echo "метрик всего: $(grep -c '^[a-z]' <<<"$body")"
if [ "$fails" -eq 0 ]; then
    echo "== стенд в порядке =="
else
    echo "== провалов: $fails ==" >&2
fi
exit "$fails"
