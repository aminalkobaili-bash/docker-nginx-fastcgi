# syntax=docker/dockerfile:1

# ---- build stage: compiler and headers stay here ----
FROM debian:13-slim AS builder

RUN apt-get update && \
    apt-get install -y --no-install-recommends gcc libc6-dev libfcgi-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY app/server.c .
RUN gcc -O2 -o server server.c -lfcgi

# ---- runtime stage: only what is needed to run ----
FROM debian:13-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        libfcgi0t64 \
        spawn-fcgi \
        nginx \
        curl && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /server

COPY --from=builder /build/server .
COPY docker/entrypoint.sh .
COPY nginx/fastcgi-gateway.conf /etc/nginx/nginx.conf

# Non-root user + no SUID/SGID binaries (Dockle CIS-DI-0001 / CIS-DI-0008)
RUN chmod +x server entrypoint.sh && \
    useradd -r -s /usr/sbin/nologin appuser && \
    chmod u-s /usr/bin/passwd \
             /usr/bin/mount \
             /usr/bin/chfn \
             /usr/bin/newgrp \
             /usr/bin/chsh \
             /usr/bin/umount \
             /usr/bin/gpasswd \
             /usr/bin/su 2>/dev/null || true && \
    chmod g-s /usr/sbin/unix_chkpwd \
             /usr/bin/expiry \
             /usr/bin/chage 2>/dev/null || true

# nginx runs as appuser: pid and temp paths live in /tmp (see nginx/fastcgi-gateway.conf)
RUN mkdir -p /var/lib/nginx /var/log/nginx /tmp/client_temp /tmp/proxy_temp \
        /tmp/fastcgi_temp /tmp/uwsgi_temp /tmp/scgi_temp && \
    chown -R appuser:appuser /var/lib/nginx /var/log/nginx /tmp

EXPOSE 81

HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD curl -f http://127.0.0.1:81/status || exit 1

USER appuser

ENTRYPOINT ["/server/entrypoint.sh"]
