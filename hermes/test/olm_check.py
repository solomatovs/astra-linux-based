"""Проверка libolm: python-olm грузит библиотеку и шифрует сообщение.

python-olm ставится из sdist и линкуется с libolm на месте, поэтому успешный
импорт означает, что и сборка биндинга в builder, и .so в рантайме на месте.
mautrix[encryption] дальше работает уже через него.
"""

import olm

alice = olm.Account()
bob = olm.Account()
bob.generate_one_time_keys(1)
otk = next(iter(bob.one_time_keys["curve25519"].values()))

session = olm.OutboundSession(alice, bob.identity_keys["curve25519"], otk)
message = session.encrypt("hermes")

inbound = olm.InboundSession(bob, message)
if inbound.decrypt(message) != "hermes":
    raise SystemExit("  FAIL  olm: расшифрованное сообщение не совпало")

print("  ok    olm-сессия шифрует и расшифровывает")
