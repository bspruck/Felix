#include "ImageBIOS.hpp"

ImageBIOS::ImageBIOS( std::vector<uint8_t> data ) : mData{ std::move( data ) }
{
}

void ImageBIOS::load( uint8_t * memory ) const
{
  auto beg = mData.cbegin();
  auto end = mData.cend();

  std::copy( beg, end, memory );
}