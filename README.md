# Docker: nginx + FastCGI (C) + Compose

**Задача:** упаковать FastCGI-сервер на C и nginx в собственный Docker-образ, довести образ до прохождения Dockle без FATAL/WARN (non-root, без SUID/SGID, healthcheck) и собрать стенд «reverse proxy + сервер» в docker-compose.

**Стек:** Docker 29 · Docker Compose v2 · Debian 13 slim · nginx 1.26 (в образе) / 1.30-alpine (прокси) · C + libfcgi · spawn-fcgi · Dockle 0.4.15
Синтаксис Dockerfile и Compose сверен с документацией Docker через Context7: multi-stage `COPY --from`, `depends_on.condition: service_healthy`, `build:` вместе с `image:`, без устаревшего поля `version:`.

## Архитектура

```
host :80 ──► proxy  (nginx:1.30-alpine, nginx/proxy.conf, :8080)
               │ proxy_pass http://server:81        сеть app-network (bridge)
               ▼
             server (docker-nginx-fastcgi:1.0, user appuser)
               nginx :81 (nginx/fastcgi-gateway.conf) ── /status → stub_status
               │ fastcgi_pass 127.0.0.1:8080
               ▼
               spawn-fcgi ─► /server/server  (app/server.c, libfcgi)
```

```
├── docker-compose.yml          # server (build) + proxy, сеть, healthcheck-зависимость
├── app/server.c                # FastCGI-сервер: цикл FCGI_Accept() → HTML «Hello, World!»
├── docker/
│   ├── Dockerfile              # multi-stage: builder (gcc) → runtime (Debian 13 slim)
│   └── entrypoint.sh           # spawn-fcgi :8080 → nginx -t → exec nginx -g "daemon off;"
└── nginx/
    ├── fastcgi-gateway.conf    # nginx внутри образа: :81 → FastCGI, /status, pid и temp-пути в /tmp
    ├── proxy.conf              # reverse proxy в compose: :8080 → server:81
    └── standalone-status.conf  # nginx.conf для официального образа: статика + /status
```

## Что сделано

1. **Работа с контейнерами вручную:** `docker pull` / `run` / `inspect` / `stop` / `restart`, проброс портов, `docker cp` своего `nginx.conf` в работающий контейнер + `nginx -s reload` (страница `/status`), `docker export` → `docker import` → запуск из нового образа.
2. **FastCGI-сервер на C:** `server.c` на libfcgi, запуск через `spawn-fcgi` на `127.0.0.1:8080`, nginx на `:81` проксирует через `fastcgi_pass`.
3. **Свой образ:** Dockerfile с компиляцией сервера при сборке, `entrypoint.sh` поднимает FastCGI и nginx в одном контейнере (`exec`, чтобы nginx получал сигналы как PID 1).
4. **Харденинг по Dockle.** Первая версия на `nginx:latest` давала:
   - `FATAL CIS-DI-0010` — ключи `NGINX_GPGKEYS` в слоях базового образа;
   - `WARN CIS-DI-0001` — запуск от root.

   Исправлено так: база заменена на `debian-slim`, добавлен пользователь `appuser`, nginx перенастроен на pid и temp-пути в `/tmp` (иначе без root он не запускается), сняты SUID/SGID-биты, добавлен `HEALTHCHECK curl :81/status`.
5. **Compose:** сервис `server` + сервис `proxy` (официальный nginx с примонтированным `proxy.conf`), общая bridge-сеть, `80:8080`.
6. **Улучшения после ревью** — ниже.

## Улучшения после ревью

| Проблема | Было | Стало |
|---|---|---|
| Проект не собирался одной командой | у `server` только `image: my-fastcgi:1.0` — образ нужно было собирать отдельно через `docker build`, `docker compose build` ничего не делал | `build: { context: ., dockerfile: docker/Dockerfile }` + `image:` → `docker compose up --build` |
| Прокси стартовал раньше готовности сервера | `depends_on: [server]` ждёт только запуска контейнера | `depends_on: { server: { condition: service_healthy } }` — в логе видно `fastcgi-server Healthy` → `nginx-proxy Starting` |
| Плавающий тег | `nginx:latest` (сейчас это mainline) | `nginx:1.30-alpine` (stable) |
| Компилятор в финальном образе | gcc и `libc6-dev` ставились в тот же образ, где работает сервер | **multi-stage:** gcc только в `builder`, в runtime — бинарник + `libfcgi0t64`, `spawn-fcgi`, `nginx`, `curl` |
| Устаревшая база | `debian:12-slim` (bookworm) | `debian:13-slim` (trixie): runtime-библиотека переименована в `libfcgi0t64` |
| Контекст сборки | `.dockerignore` нет, в контекст уходил `container.tar` на 156 MB | `.dockerignore` пропускает только `app/`, `docker/`, `nginx/` |

**Размер образа** — тот же код, замер `docker images` на Docker Desktop 29.0.1:

| Образ | База | gcc внутри | `docker images` | сжатый (`docker image inspect .Size`) |
|---|---|---|---|---|
| до (single-stage) | Debian 12, nginx 1.22.1 | `/usr/bin/gcc` | **397 MB** | 102 MB |
| после (multi-stage) | Debian 13, nginx 1.26.3 | нет | **149 MB** (−62%) | 38 MB (−63%) |

## Как запустить

```bash
docker compose up --build -d
curl localhost/          # Hello, World! от FastCGI-сервера
curl localhost/status    # nginx stub_status
docker compose down
```

Конфиг для официального образа nginx (пункт 1):

```bash
docker run -d --name nginx-status -p 8089:80 \
  -v "$(pwd)/nginx/standalone-status.conf:/etc/nginx/nginx.conf:ro" nginx:1.30-alpine
curl localhost:8089/status
```

## Результат

Реальный вывод прогона после изменений:

```console
$ docker compose up --build -d
 docker-nginx-fastcgi:1.0  Built
 Network docker-nginx-fastcgi_app-network  Created
 Container fastcgi-server  Created
 Container nginx-proxy  Created
 Container fastcgi-server  Started
 Container fastcgi-server  Waiting
 Container fastcgi-server  Healthy
 Container nginx-proxy  Starting
 Container nginx-proxy  Started

$ docker compose ps
NAME             IMAGE                      SERVICE   STATUS                   PORTS
fastcgi-server   docker-nginx-fastcgi:1.0   server    Up 9 seconds (healthy)   81/tcp
nginx-proxy      nginx:1.30-alpine          proxy     Up 3 seconds             0.0.0.0:80->8080/tcp, [::]:80->8080/tcp

$ curl -s localhost/
<html><head><title>Hello, World!</title></head><body><h1>Hello, World!</h1><p>This is a FastCGI server running on port 8080</p></body></html>

$ curl -s localhost/status
Active connections: 1
server accepts handled requests
 3 3 3
Reading: 0 Writing: 1 Waiting: 0

$ docker exec fastcgi-server ps -eo user,pid,args
appuser 1  nginx: master process nginx -g daemon off;
appuser 8  /server/server
appuser 10 nginx: worker process

$ docker images
REPOSITORY             TAG   SIZE
docker-nginx-fastcgi   1.0   149MB

$ dockle --input docker-nginx-fastcgi.tar      # docker save → dockle 0.4.15
INFO	- CIS-DI-0005: Enable Content trust for Docker
	* export DOCKER_CONTENT_TRUST=1 before docker pull/build
```

- Ни FATAL, ни WARN от Dockle нет ни до, ни после multi-stage — харденинг сохранился. `CIS-DI-0005` — информационное сообщение о Docker Content Trust.
- Все процессы в контейнере работают от `appuser`, не от root.
- `nginx/standalone-status.conf` проверен на `nginx:1.30-alpine`: `/status` отвечает, `/` возвращает 200.

## Ограничения

- nginx и FastCGI-процесс работают в одном контейнере (так требует задание). Канонический вариант — по процессу на контейнер; здесь `spawn-fcgi` форкает сервер, а PID 1 — nginx.
- FastCGI-сервер отвечает одинаковой страницей на любой путь (`/anything` → 200).
- `HEALTHCHECK` требует `curl` в runtime-образе.
