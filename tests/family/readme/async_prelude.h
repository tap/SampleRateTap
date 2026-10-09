// The free names async/README.md's quick start uses: the caller's buffers.
#include <cstddef>
#include <vector>
static std::vector<float> g_in(960), g_out(960);
const float*              input_interleaved  = g_in.data();
float*                    output_interleaved = g_out.data();
std::size_t               frames             = 480;
