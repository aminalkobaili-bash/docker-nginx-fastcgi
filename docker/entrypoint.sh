#!/bin/bash

set -e

echo "Starting FastCGI server..."

spawn-fcgi -a 127.0.0.1 -p 8080 -f /server/server

echo "FastCGI server started."

echo "Testing nginx configuration..."
nginx -t

echo "Starting nginx..."

exec nginx -g "daemon off;"