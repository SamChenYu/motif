#include "git.hpp"

#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>

std::string capture_output(const std::string& cmd) {
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) throw std::runtime_error("popen failed: " + cmd);

    std::array<char, 64 * 1024> buf{};
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        out.append(buf.data(), n);
    }

    if (const int rc = pclose(pipe); rc != 0) throw std::runtime_error("command failed: " + cmd);
    return out;
}