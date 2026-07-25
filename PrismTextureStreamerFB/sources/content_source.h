#pragma once
#include <cstdint>
#include <vector>
#include <Windows.h>

// A content source produces RGBA8 frames of a fixed size on its own worker thread
class IContentSource
{
public:
    virtual ~IContentSource() = default;

    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;

    virtual void SetFramerate(uint8_t framerate) = 0;
    virtual bool CopyLatestFrame(std::vector<uint8_t>& dst) = 0;
};