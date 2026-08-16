#include "status.hpp"
#include "../cli/git.hpp"


#include <iostream>

int status(int argc, char* argv[]) {



    std::vector<std::string> files = files_changed();

    for (const auto& file : files) {
        if ( is_musescore_file(file) ) {
            std::pair<std::string, std::string> head_working_files = get_head_and_working_tree(file);

            std::cout << head_working_files.first << std::endl;
            std::cout << head_working_files.second << std::endl;
        }
    }




    return 0;
}