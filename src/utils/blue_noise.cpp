#include "blue_noise.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <iostream>

namespace cg::utils
{
    blue_noise::blue_noise(const std::string& path)
    {
        int channels;
        unsigned char* img = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (!img)
        {
            std::cerr << "Failed to load blue noise texture: " << path << std::endl;
            return;
        }
        data.assign(img, img + width * height * 4);
        stbi_image_free(img);
        std::cout << "Loaded blue noise: " << width << "x" << height << std::endl;
    }

    float3 blue_noise::get_value(int x, int y) const
    {
        x = x % width;
        y = y % height;
        int index = (y * width + x) * 4;
        return float3(
            data[index] / 255.f,
            data[index + 1] / 255.f,
            data[index + 2] / 255.f
        );
    }
}
