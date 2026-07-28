# uv

Сборка [astral-sh/uv](https://github.com/astral-sh/uv) из исходников. Результат —
образы `dmp/uv:<версия>` и `dmp/uv:<версия>-dist` с бинарями `uv` и `uvx` поверх
чистого `dmp/glibc:2.28`.

## Команды

| Команда | Что делает |
|---------|------------|
| `make help` | список команд (по умолчанию) |
| `make deps` | показать MSRV исходников и сверить с rustc из build-образа |
| `make sources` | скачать исходники и завендорить крейты в `$(ARTIFACTS)/src/` |
| `make dist` | собрать `dmp/uv:<версия>-dist` |
| `make build` | собрать `dmp/uv:<версия>` |
| `make test` | uv/uvx --version, порог glibc, `uv lock` без сети |
| `make glibc` | максимальная версия символов `GLIBC_`, которую требуют бинари |

## Оффлайн-сборка

Сеть нужна только на шаге `make sources`: он качает тарбол релиза, прогоняет
`cargo vendor --versioned-dirs --locked` внутри `dmp/rust` и пакует исходники
вместе с `vendor/` в `$(ARTIFACTS)/src/uv-<версия>.tar.gz` (~103 МБ, сам `vendor/` — 835 МБ).
Контейнер вендоринга работает на обычной bridge-сети; если нужен прокси — он задаётся
монтируемым `cargo-config.toml` (см. ниже), а не `--network host`.

Дальше `dist`/`build` идут без сети: `.cargo/config.toml` в тарболе подменяет
`source crates-io` на `vendor/`, в Dockerfile выставлен `CARGO_NET_OFFLINE=true`,
сборка идёт `cargo build --release --offline --locked`.

Других каналов в сеть у сборки нет: git-зависимостей в `Cargo.lock` нет,
`crates/uv/build.rs` работает только под Windows, нативный код (jemalloc, ring)
вендорит свои исходники. `cargo test` не запускается — тесты uv ходят в PyPI.

## Потолок версии

uv объявляет MSRV в `Cargo.toml`, и растёт он быстрее, чем цепочка соседнего
проекта `rust`. В контуре есть только `dmp/rust:1.90.0`, поэтому потолок —
**uv 0.9.26** (MSRV 1.89). Таблица версий — в `Makefile`, проверка — `make deps`.

`rust-toolchain.toml` в исходниках uv не ограничивает ничего: его читает rustup,
а rustup в образах не установлен.

Чтобы поднять версию uv, надо сначала продлить цепочку `astra-linux-based/rust`,
затем раскомментировать нужную строку `DEPS_<версия>`.

## Грабли mrustc-сборки rustc

`rustc` из `dmp/rust` собран через mrustc, и это дало два дефекта:

* `rustc --version` печатает `rustc 1.90.0-stable-mrustc` — суффикс стоит на месте
  канала и валит `build.rs` у `rustversion`, `version_check` и прочих читателей строки;
* std собран без LLVM-биткода в rlib, поэтому объявленный в uv `lto = "fat"`
  падает на `Can't find section .llvmbc`.

Причина обоих — две строки в `run_rustc/Makefile` самого mrustc:
`CFG_VERSION=$(RUSTC_VERSION)-stable-mrustc` и `RUSTFLAGS` без `-C embed-bitcode=yes`.
**Починено в `astra-linux-based/rust/Dockerfile`** — патчем этих строк перед сборкой,
там же стоят проверки результата (`rustc --version` строго равен `rustc <версия>`,
в `libstd.rlib` есть секция `.llvmbc`).

Образ `dmp/rust:1.90.0` этим патчем **ещё не пересобран** (сборка через mrustc идёт
часами), поэтому в uv пока живут два временных обхода: шим `/opt/rustc-shim/rustc`
и `CARGO_PROFILE_RELEASE_LTO=off`. После пересборки `dmp/rust` оба надо снять из
`Dockerfile` и убедиться, что `make build` проходит.

## Минимальный glibc

Порог задаёт весь тулчейн: `dmp/rust:1.90.0` собран на `dmp/gcc:8.5.0`, тот — на
`dmp/glibc:2.28` (Astra Orel 2.12). Ниже 2.28 не опуститься, не пересобрав цепочку.
Фактический порог считается на этапе builder и лежит в образе файлом
`/usr/local/share/uv/glibc-floor.txt`, показать — `make glibc`.

Статическая линковка glibc не делается намеренно: uv резолвит DNS через
`getaddrinfo`, статический glibc ломает NSS. Полностью статический бинарь
потребовал бы musl-таргета, которого в контуре нет.

## Закрытый контур

Два конфига, по одному на каждую сторону.

**Сборка** — `cargo-config.toml.example`. Реальный файл кладётся рядом с Makefile
как `cargo-config.toml` и монтируется в `$CARGO_HOME/config.toml` контейнера на шаге
`make sources` (proxy, замена источника на nexus, свой CA). Монтируется только если
файл есть, проверить — `make debug`.

**Рантайм** — `uv-config.toml.example`. Реальный файл кладётся в
`$(ARTIFACTS)/uv-config.toml` и попадает в образ как `/etc/uv/uv.toml` (index на nexus,
`native-tls`, зеркало для `uv python install`).

По умолчанию нет ни того, ни другого.
