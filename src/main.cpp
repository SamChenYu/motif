#include <string>
#include <vector>

#include "motif/cli/dispatch.h"

int main(int argc, char** argv) {
  return motif::cli::dispatch(std::vector<std::string>(argv + 1, argv + argc));
}
