"""Проверка, что hermes находит рабочий agent-browser.

Резолвер перебирает кандидатов (node_modules/.bin, PATH, npx) и проверяет
каждого запуском --version. В образе на PATH лежит musl-сборка: обычный бинарь
пакета требует GLIBC новее базового.
"""

import sys

from hermes_constants import agent_browser_runnable
from tools.browser_tool import _find_agent_browser

path = _find_agent_browser()
if not path:
    sys.exit("  FAIL  hermes не нашёл agent-browser")
if not agent_browser_runnable(path):
    sys.exit(f"  FAIL  agent-browser не запускается: {path}")

print(f"  ok    hermes резолвит agent-browser: {path}")
