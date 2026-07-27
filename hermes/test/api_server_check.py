"""Проверка платформы api_server: адаптер собирается и отдаёт свои маршруты.

Сеть не поднимается — connect() требует ключа и живого gateway; здесь важно,
что модуль импортируется со всеми зависимостями (aiohttp) и таблица маршрутов
содержит эндпоинты, ради которых образ и собирается.
"""

import sys

from gateway.config import Platform, PlatformConfig
from gateway.platforms import api_server

REQUIRED_ROUTES = [
    ("GET", "/health"),
    ("GET", "/v1/models"),
    ("POST", "/v1/chat/completions"),
    ("POST", "/v1/runs"),
    ("GET", "/v1/runs/{run_id}/events"),
    ("POST", "/v1/runs/{run_id}/stop"),
    ("GET", "/api/sessions"),
    ("POST", "/api/sessions"),
    ("GET", "/api/sessions/{session_id}/messages"),
]

if not api_server.AIOHTTP_AVAILABLE:
    sys.exit("  FAIL  aiohttp недоступен, платформа api_server не поднимется")

adapter = api_server.APIServerAdapter(PlatformConfig(enabled=True))
if adapter.platform is not Platform.API_SERVER:
    sys.exit(f"  FAIL  адаптер зарегистрирован как {adapter.platform}")

routes = {(method, path) for method, path, _ in adapter._http_route_table()}
missing = [r for r in REQUIRED_ROUTES if r not in routes]
if missing:
    sys.exit(f"  FAIL  нет маршрутов: {missing}")

print(f"  ok    api_server: {len(routes)} маршрутов, все обязательные на месте")
