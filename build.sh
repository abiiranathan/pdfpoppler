#!/usr/bin/env bash

# Build the project
echo "Building the project..."

gcc -g example.c src/pdfpoppler.c $(pkg-config --cflags --libs glib-2.0 gio-2.0 cairo poppler-glib)

./a.out