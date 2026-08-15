#include "status.hpp"
#include "../cli/git.hpp"


#include <iostream>

int status(int argc, char* argv[]) {

    std::string status_output{capture_output("git status")};





    return 0;
}