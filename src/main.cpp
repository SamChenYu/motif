#include <iostream>
#include <array>
#include <string>

// Placeholder forward declarations
void status(int, char*[]) {
    std::cout << "status" << std::endl;
}
void diff(int, char*[]) {
    std::cout << "diff" << std::endl;
}
void logg(int, char*[]) {
    std::cout << "logg" << std::endl;
}
void inspect(int, char*[]) {
    std::cout << "inspect" << std::endl;
}

static constexpr std::array<std::pair<std::string_view, void(*)(int, char*[])>, 4> kCommands{
            std::pair{"status", &status},
            std::pair{"diff", &diff},
            std::pair{"log", &logg},
            std::pair{"inspect", &inspect},
};

static constexpr std::string kVersion{"0.1.0"};

int main(int argc, char* argv[]) {

    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "help") == 0)  {
        std::cout << "Usage: " << argv[0] << " <command>" << std::endl;
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        std::cout << "Motif Version: " << kVersion << std::endl;
        return 0;
    }

    for (const auto &[fst, snd] : kCommands) {
        if (fst == argv[1]) {
            snd(argc, argv);
            return 0;
        }
    }

    std::cout << "Unrecognized command: " << argv[1] << std::endl;
    return 1;
}