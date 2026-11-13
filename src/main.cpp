#include "cli.hpp"

int main(int argc, char** argv) {
  std::ios::sync_with_stdio(false);
  return cli::run(argc, argv);
}
