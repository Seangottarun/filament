/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TNT_FILAMENT_DRIVER_DATARESHAPER_H
#define TNT_FILAMENT_DRIVER_DATARESHAPER_H

#include <stddef.h>

#include <math/scalar.h>

namespace filament {
namespace backend {

// This provides an alpha value when expanding 3-channel images to 4-channel images, and is also
// used as a normalization scale when converting between numeric types. Note that
// std::numeric_limits<componentType>::max() is not appropriate for floats.
template<typename dstComponentType> inline dstComponentType getMaxValue();
template<> inline float getMaxValue() { return 1.0f; }
template<> inline int32_t getMaxValue() { return 0x7fffffff; }
template<> inline uint32_t getMaxValue() { return 0xffffffff; }
template<> inline uint16_t getMaxValue() { return 0x3c00; } // 0x3c00 is 1.0 in half-float.
template<> inline uint8_t getMaxValue() { return 0xff; }

// This little utility adds padding to multi-channel interleaved data by inserting dummy values, or
// discards trailing channels. This is useful for platforms that only accept 4-component data, since
// users often wish to submit (or receive) 3-component data.
class DataReshaper {
public:
    template<typename componentType, size_t srcChannelCount, size_t dstChannelCount>
    static void reshape(void* dest, const void* src, size_t numSrcBytes) {
        const componentType maxValue = getMaxValue<componentType>();
        const componentType* in = (const componentType*) src;
        componentType* out = (componentType*) dest;
        const size_t srcWordCount = (numSrcBytes / sizeof(componentType)) / srcChannelCount;
        const int minChannelCount = filament::math::min(srcChannelCount, dstChannelCount);
        for (size_t word = 0; word < srcWordCount; ++word) {
            for (size_t channel = 0; channel < minChannelCount; ++channel) {
                out[channel] = in[channel];
            }
            for (size_t channel = srcChannelCount; channel < dstChannelCount; ++channel) {
                out[channel] = maxValue;
            }
            in += srcChannelCount;
            out += dstChannelCount;
        }
    }

    template<typename dstComponentType, typename srcComponentType>
    static void reshapeImage(uint8_t* dest, const uint8_t* src,  size_t srcBytesPerRow,
            size_t dstBytesPerRow, size_t dstChannelCount, size_t height, bool swizzle03) {
        const size_t srcChannelCount = 4;
        const dstComponentType dstMaxValue = getMaxValue<dstComponentType>();
        const srcComponentType srcMaxValue = getMaxValue<srcComponentType>();
        const size_t width = (srcBytesPerRow / sizeof(dstComponentType)) / srcChannelCount;
        const size_t minChannelCount = filament::math::min(srcChannelCount, dstChannelCount);
        assert(minChannelCount <= 4);
        const int inds[4] = {swizzle03 ? 2 : 0, 1, swizzle03 ? 0 : 2, 3};
        for (size_t row = 0; row < height; ++row) {
            const srcComponentType* in = (const srcComponentType*) src;
            dstComponentType* out = (dstComponentType*) dest;
            for (size_t column = 0; column < width; ++column) {
                for (size_t channel = 0; channel < minChannelCount; ++channel) {
                    // out[channel] = (dstComponentType) ((double) in[inds[channel]] * (double) dstMaxValue / (double) srcMaxValue);
                    out[channel] = in[inds[channel]] * dstMaxValue / srcMaxValue;
                }
                for (size_t channel = srcChannelCount; channel < dstChannelCount; ++channel) {
                    out[channel] = dstMaxValue;
                }
                in += srcChannelCount;
                out += dstChannelCount;
            }
            src += srcBytesPerRow;
            dest += dstBytesPerRow;
        }
    }

    static bool reshapeImage(PixelBufferDescriptor* dst, PixelDataType srcType,
            const uint8_t* srcBytes, int srcBytesPerRow, int width, int height, bool swizzle) {
        size_t dstChannelCount;
        switch (dst->format) {
            case PixelDataFormat::RGB: dstChannelCount = 3; break;
            case PixelDataFormat::RGBA: dstChannelCount = 4; break;
            default: return false;
        }
        void (*reshaper)(uint8_t*, const uint8_t*, size_t, size_t, size_t, size_t, bool) = nullptr;
        constexpr auto UBYTE = PixelDataType::UBYTE, FLOAT = PixelDataType::FLOAT,
                UINT = PixelDataType::UINT, INT = PixelDataType::INT;
        switch (dst->type) {
            case UBYTE:
                switch (srcType) {
                    case UBYTE: reshaper = reshapeImage<uint8_t, uint8_t>; break;
                    case FLOAT: reshaper = reshapeImage<uint8_t, float>; break;
                    case INT: reshaper = reshapeImage<uint8_t, int32_t>; break;
                    case UINT: reshaper = reshapeImage<uint8_t, uint32_t>; break;
                    default: return false;
                }
                break;
            case FLOAT:
                switch (srcType) {
                    case UBYTE: reshaper = reshapeImage<float, uint8_t>; break;
                    case FLOAT: reshaper = reshapeImage<float, float>; break;
                    case INT: reshaper = reshapeImage<float, int32_t>; break;
                    case UINT: reshaper = reshapeImage<float, uint32_t>; break;
                    default: return false;
                }
                break;
            case INT:
                switch (srcType) {
                    case UBYTE: reshaper = reshapeImage<int32_t, uint8_t>; break;
                    case FLOAT: reshaper = reshapeImage<int32_t, float>; break;
                    case INT: reshaper = reshapeImage<int32_t, int32_t>; break;
                    case UINT: reshaper = reshapeImage<int32_t, uint32_t>; break;
                    default: return false;
                }
                break;
            case UINT:
                switch (srcType) {
                    case UBYTE: reshaper = reshapeImage<uint32_t, uint8_t>; break;
                    case FLOAT: reshaper = reshapeImage<uint32_t, float>; break;
                    case INT: reshaper = reshapeImage<uint32_t, int32_t>; break;
                    case UINT: reshaper = reshapeImage<uint32_t, uint32_t>; break;
                    default: return false;
                }
                break;
            default:
                return false;
        }
        uint8_t* dstBytes = (uint8_t*) dst->buffer;
        const int dstBytesPerRow = PixelBufferDescriptor::computeDataSize(dst->format, dst->type,
                dst->stride ? dst->stride : width, 1, dst->alignment);
        reshaper(dstBytes, srcBytes, srcBytesPerRow, dstBytesPerRow, dstChannelCount, height,
                swizzle);
        return true;
    }

};

} // namespace backend
} // namespace filament

#endif // TNT_FILAMENT_DRIVER_DATARESHAPER_H
