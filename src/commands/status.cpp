#include "status.hpp"
#include "../cli/git.hpp"


#include <iostream>

struct file_comparison; // Forward declaration

int status(int argc, char* argv[]) {

    std::vector<std::string> files = files_changed();

    for (const auto& file : files) {
        if ( is_musescore_file(file) ) {
            file_comparison head_working_files = get_head_and_working_tree(file);

            std::cout << head_working_files.m_file_name << std::endl;
            std::cout << head_working_files.m_head_contents << std::endl;
            std::cout << head_working_files.m_working_contents << std::endl;
        }
    }




    return 0;
}