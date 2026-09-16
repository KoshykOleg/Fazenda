#pragma once
#include <string>
#include <functional>

namespace tf {

extern bool verbose;
extern void (*failContext)();

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
void check(bool cond, const std::string& desc);
void observe(const std::string& msg);
void info(const std::string& msg);
void suite(const std::string& name);
void run(const std::string& name, const std::function<void()>& fn);
int summary();

}

#define CHECK(cond, ...) tf::check(static_cast<bool>(cond), tf::fmt(__VA_ARGS__))
#define OBSERVE(...) tf::observe(tf::fmt(__VA_ARGS__))
#define INFO(...) tf::info(tf::fmt(__VA_ARGS__))
