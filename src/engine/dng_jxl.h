// SPDX-License-Identifier: GPL-3.0-or-later
// Original OmaRAW integration, developed with AI assistance for local review.
// This product includes DNG technology under license by Adobe.
#pragma once
#include <jxl/decode.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace oma_dng {
// DNG tiles carry sample values, not display pixels: never apply orientation,
// colour conversion or full-range integer rescaling inside the JPEG XL decoder.
inline bool decodeJxl(const uint8_t *bytes, size_t length, uint32_t width,
                      uint32_t height, uint32_t channels, uint32_t bits,
                      bool floating, std::vector<uint8_t> &result)
{
  result.clear();
  if(!bytes || !length || !width || !height || (channels!=1 && channels!=3)
     || bits<8 || bits>16 || (floating && bits!=16)
     || uint64_t(width)*height*channels*(floating ? 4 : 2)>512ULL*1024*1024) return false;
  std::unique_ptr<JxlDecoder, decltype(&JxlDecoderDestroy)> decoder(JxlDecoderCreate(nullptr), JxlDecoderDestroy);
  if(!decoder) return false;
  auto *d=decoder.get();
  if(JxlDecoderSetKeepOrientation(d,JXL_TRUE)!=JXL_DEC_SUCCESS
     || JxlDecoderSubscribeEvents(d,JXL_DEC_BASIC_INFO|JXL_DEC_FULL_IMAGE)!=JXL_DEC_SUCCESS
     || JxlDecoderSetInput(d,bytes,length)!=JXL_DEC_SUCCESS) return false;
  JxlDecoderCloseInput(d);
  bool infoSeen=false, bufferSet=false, imageSeen=false;
  const JxlPixelFormat format{channels, floating ? JXL_TYPE_FLOAT : JXL_TYPE_UINT16, JXL_NATIVE_ENDIAN, 0};
  for(;;) {
    const auto status=JxlDecoderProcessInput(d);
    if(status==JXL_DEC_BASIC_INFO) {
      JxlBasicInfo info{};
      if(infoSeen || JxlDecoderGetBasicInfo(d,&info)!=JXL_DEC_SUCCESS
         || info.xsize!=width || info.ysize!=height || info.num_color_channels!=channels
         || info.num_extra_channels || info.have_animation
         || (floating ? !((info.bits_per_sample==16 && info.exponent_bits_per_sample==5)
                           || (info.bits_per_sample==32 && info.exponent_bits_per_sample==8))
                      : (info.bits_per_sample!=bits || info.exponent_bits_per_sample!=0))) return false;
      infoSeen=true;
    } else if(status==JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
      size_t size=0;
      if(!infoSeen || bufferSet || JxlDecoderImageOutBufferSize(d,&format,&size)!=JXL_DEC_SUCCESS
         || size!=size_t(width)*height*channels*(floating ? 4 : 2)) return false;
      result.resize(size);
      if(JxlDecoderSetImageOutBuffer(d,&format,result.data(),size)!=JXL_DEC_SUCCESS) return false;
      if(!floating) {
        const JxlBitDepth depth{JXL_BIT_DEPTH_FROM_CODESTREAM,0,0};
        if(JxlDecoderSetImageOutBitDepth(d,&depth)!=JXL_DEC_SUCCESS) return false;
      }
      bufferSet=true;
    } else if(status==JXL_DEC_FULL_IMAGE) {
      if(!bufferSet || imageSeen) return false;
      imageSeen=true;
    } else if(status==JXL_DEC_SUCCESS) return imageSeen;
    else return false;
  }
}

// Index in the stored, field-grouped axis, including non-divisible dimensions.
inline uint32_t interleavedIndex(uint32_t coordinate, uint32_t length, uint32_t factor)
{
  const uint32_t field=coordinate%factor;
  return field*(length/factor)+std::min(field,length%factor)+coordinate/factor;
}
}
