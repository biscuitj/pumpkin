// The dashboard pages, built from ../dashboard and embedded by
// scripts/embed_dashboard.py into web_assets.cpp (generated, not in git).
#pragma once

#include <stddef.h>
#include <stdint.h>

struct WebAsset {
    const char* path;      // "/index.html", "/setup.html"
    const uint8_t* data;
    size_t size;
    const char* mime;
    const char* etag;      // quoted
    bool gzip;
};

extern const WebAsset kWebAssets[];
extern const size_t kWebAssetCount;

const WebAsset* findWebAsset(const char* path);
