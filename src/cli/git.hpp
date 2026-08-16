#pragma once

#include <string>
#include <vector>
#include <fstream>

struct file_comparison {

    file_comparison(const std::string& file_name, const std::string& head_contents, const std::string& working_contents) :
        m_file_name{file_name}, m_head_contents{head_contents}, m_working_contents{working_contents} {};

    std::string m_file_name;
    std::string m_head_contents;
    std::string m_working_contents;
};

std::pair<std::string, int> capture_output(const std::string& cmd);

std::optional<std::string> read_working_file(const std::filesystem::path& path);

std::vector<std::string> files_changed();

bool is_musescore_file(const std::string& path);

file_comparison get_head_and_working_tree(const std::string& path);