#pragma once

#include <string>
#include <vector>
#include <fstream>

struct file_revision {
    std::string file_name;
    std::string blob_contents;
};

std::pair<std::string, int> capture_output(const std::string& cmd);

std::optional<std::string> read_working_file(const std::filesystem::path& path);

std::vector<std::string> files_changed();

bool is_musescore_file(const std::string& path);

std::pair<std::string, std::string> get_head_and_working_tree(const std::string& path);