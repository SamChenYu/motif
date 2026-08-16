#include "git.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <ranges>

// API Contract: Let consumers decide whether the error is ok or not
std::pair<std::string, int> capture_output(const std::string& cmd) {
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) throw std::runtime_error("popen failed: " + cmd);

    std::array<char, 64 * 1024> buf{};
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        out.append(buf.data(), n);
    }

    const int rc = pclose(pipe);
    //if (const int rc = pclose(pipe); rc != 0) throw std::runtime_error("command failed: " + cmd);
    return {out, rc};
}

std::optional<std::string> read_working_file(const std::filesystem::path& path){
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<std::string> files_changed() {
    std::vector<std::string> files;
    std::pair<std::string, int> output{capture_output("git diff --name-only")};
    for (auto part : output.first | std::views::split('\n')) {
        std::string str(part.begin(), part.end());
        files.emplace_back(str);
    }
    return files;
}

bool is_musescore_file(const std::string& path) {
    return path.ends_with(".mscx") || path.ends_with(".mscz");
}

std::pair<std::string, std::string> get_head_and_working_tree(const std::string& path) {

    if (!path.ends_with(".mscx") || !path.ends_with(".mscz"))
        throw std::runtime_error{"get_head_and_working_tree() called with non .mscz or .mscx formats!"};

    // Firstly check rev-parse that it is inside HEAD and CWD.
    // Todo: This isn't strictly correct but for most cases when file is present in HEAD and CWD it is fine
    std::pair<std::string, int> verify = capture_output("git rev-parse --quiet --verify \"HEAD:" + path + "\"");
    std::optional<std::string> working_file = read_working_file(path);

    // File not in HEAD and not CWD (technically shouldn't be possible because path was gathered from git diff)
    if (verify.second != 0 and !working_file)
        return {"", ""};

    // File not in HEAD but in CWD
    if (verify.second != 0)
        return {"", working_file.value()};

    // File in HEAD and in CWD

    if (path.ends_with(".mscx")) { // Todo: assuming that the files don't change from .mscx to .mscz in the commit
         std::pair<std::string, int> blob = capture_output("git cat-file blob \"HEAD:" + path + "\"");
        if (blob.second != 0)
            throw std::runtime_error{"Command failed: git cat-file blob " + path + " not found"};
        return {blob.first, working_file.value()};
    }

    // .mscz requires unzipping
    std::pair<std::string, int> blob = capture_output("git cat-file blob \"HEAD:" + path + "\"");
    if (blob.second != 0)
        throw std::runtime_error{"Command failed: git cat-file blob " + path + " not found"};
    // Unzip both HEAD and working
    // Er




    return {};
}