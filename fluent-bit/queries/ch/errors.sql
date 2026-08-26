-- Счётчики ошибок с момента старта ноды. Растёт value — что-то пошло не так
-- именно сейчас; само по себе ненулевое значение ещё ни о чём не говорит.
-- метки: name   значения: value, remote
SELECT
    hostName() AS instance,
    name,
    value,
    remote
FROM system.errors
WHERE value > 0
ORDER BY value DESC
LIMIT 100
FORMAT JSONEachRow
