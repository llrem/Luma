#pragma once

#include <string>
#include <memory>
#include <stdexcept>
#include <stb_image.h>


class Image {
public:
    Image() : mWidth(0), mHeight(0), mChannels(0), mIsHDR(false) {}

    int width() const { return mWidth; }

    int height() const { return mHeight; }

    int channels() const { return mChannels; }

    bool isHDR() const { return mIsHDR; }

    template<typename T> 
    const T *pixels() const { 
        return reinterpret_cast<const T *>(mPixels.get());
    }

    int bytesPerPixel() { 
        return mChannels * (mIsHDR ? sizeof(float) : sizeof(unsigned char));
    }

    int pitch() { return mWidth * bytesPerPixel(); }

    static std::shared_ptr<Image> fromFile(const std::string& filename, int channels = 4) {
        std::shared_ptr<Image> image = std::make_shared<Image>();

        if (stbi_is_hdr(filename.c_str())) {
            float *pixels = stbi_loadf(filename.c_str(), &image->mWidth, &image->mHeight, &image->mChannels, channels);
            if (pixels) {
                image->mPixels.reset(reinterpret_cast<unsigned char *>(pixels));
                image->mIsHDR = true;
            }
        } else {
            unsigned char *pixels =
                stbi_load(filename.c_str(), &image->mWidth, &image->mHeight, &image->mChannels, channels);
            if (pixels) {
                image->mPixels.reset(pixels);
                image->mIsHDR = false;
            }
        }

        if (channels > 0) {
            image->mChannels = channels;
        }
        if (!image->mPixels) {
            throw std::runtime_error("Failed to load image file: " + filename);
        }
        return image;
    }

private:
    int mWidth, mHeight, mChannels;
    bool mIsHDR;
    std::unique_ptr<unsigned char> mPixels;
};