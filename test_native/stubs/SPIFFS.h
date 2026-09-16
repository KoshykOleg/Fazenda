#pragma once
#include <Arduino.h>
#include <map>
#include <string>

#define FILE_READ   "r"
#define FILE_WRITE  "w"
#define FILE_APPEND "a"

struct FakeFsState {
    std::map<std::string, std::string> files;
    bool mountOk = true;
    bool failOpen = false;
    size_t total = 1318001;
    long opens = 0;
};
extern FakeFsState g_fs;

class File {
public:
    File() {}
    File(const std::string& p, char m) : path_(p), mode_(m), valid_(true) {}
    explicit operator bool() const { return valid_; }
    size_t size() const {
        if (!valid_) return 0;
        auto it = g_fs.files.find(path_);
        return it == g_fs.files.end() ? 0 : it->second.size();
    }
    bool seek(size_t p) { if (!valid_) return false; pos_ = p; return true; }
    int available() {
        if (!valid_) return 0;
        size_t n = size();
        return pos_ < n ? (int)(n - pos_) : 0;
    }
    size_t read(uint8_t* buf, size_t len) {
        if (!valid_ || mode_ != 'r') return 0;
        const std::string& d = g_fs.files[path_];
        if (pos_ >= d.size()) return 0;
        size_t n = std::min(len, d.size() - pos_);
        memcpy(buf, d.data() + pos_, n);
        pos_ += n;
        return n;
    }
    size_t write(const uint8_t* buf, size_t len) {
        if (!valid_ || mode_ == 'r') return 0;
        g_fs.files[path_].append((const char*)buf, len);
        return len;
    }
    size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
    size_t println(const char* s) { return print(s) + print("\r\n"); }
    __attribute__((format(printf, 2, 3)))
    size_t printf(const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n < 0) return 0;
        return print(buf);
    }
    void close() { valid_ = false; }

private:
    std::string path_;
    char mode_ = 'r';
    bool valid_ = false;
    size_t pos_ = 0;
};

class SPIFFSClass {
public:
    bool begin(bool = false) { return g_fs.mountOk; }
    size_t totalBytes() { return g_fs.total; }
    size_t usedBytes() {
        size_t n = 0;
        for (const auto& f : g_fs.files) n += f.second.size();
        return n;
    }
    bool exists(const char* p) { return g_fs.files.count(p) > 0; }
    File open(const char* p, const char* mode) {
        g_fs.opens++;
        if (g_fs.failOpen) return File();
        std::string path(p);
        if (mode[0] == 'r') {
            if (!g_fs.files.count(path)) return File();
            return File(path, 'r');
        }
        if (mode[0] == 'w') {
            g_fs.files[path].clear();
            return File(path, 'w');
        }
        g_fs.files[path];
        return File(path, 'a');
    }
    bool remove(const char* p) { return g_fs.files.erase(p) > 0; }
    bool rename(const char* a, const char* b) {
        auto it = g_fs.files.find(a);
        if (it == g_fs.files.end()) return false;
        std::string d = it->second;
        g_fs.files.erase(it);
        g_fs.files[b] = d;
        return true;
    }
};
extern SPIFFSClass SPIFFS;
