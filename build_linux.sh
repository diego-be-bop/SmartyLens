#!/bin/sh
# Compila la versione Linux. Pacchetti necessari (Debian/Ubuntu):
#   sudo apt install gcc pkg-config libgtk-3-dev libcurl4-openssl-dev
set -e
cd "$(dirname "$0")"
gcc -Os -s -Wall -Wno-deprecated-declarations -Wno-format-truncation -o smartylens smartylens_linux.c \
    $(pkg-config --cflags --libs gtk+-3.0 libcurl) -lpthread
echo "OK: $(pwd)/smartylens ($(du -k smartylens | cut -f1) KB)"
