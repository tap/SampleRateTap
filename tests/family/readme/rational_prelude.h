// The free names rational/README.md's quick start uses: the caller's input.
#include <cstddef>
#include <vector>
static std::vector<float> g_in(960);
const float*              in   = g_in.data();
std::size_t               n_in = 480;
