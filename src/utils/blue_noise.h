#pragma once
#include <linalg.h>
#include <vector>
#include <string>

using namespace linalg::aliases;

namespace cg::utils
{
    class blue_noise
    {
    public:
        blue_noise(const std::string& path);
        float3 get_value(int x, int y) const;

    private:
        int width = 0;
        int height = 0;
        std::vector<unsigned char> data;
    };
}
