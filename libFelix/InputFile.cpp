#include "InputFile.hpp"
#include "ImageBS93.hpp"
#include "ImageCart.hpp"
#include "Utility.hpp"
#include "ImageProperties.hpp"
#include "Log.hpp"

InputFile::InputFile( std::filesystem::path const & path, std::shared_ptr<ImageProperties> & imageProperties ) : mType{}, mBS93{}, mCart{}
{
  auto data = readFile( path );

  if ( data.empty() )
    return;

  bool propsReset = false;

  if ( imageProperties && imageProperties->getPath() != path )
  {
    imageProperties.reset();
  }

  if ( !imageProperties )
  {
    imageProperties = std::make_shared<ImageProperties>( path );
    propsReset = true;
  }

  if ( auto pCart = ImageCart::create( data ) )
  {
    if ( propsReset )
    {
      pCart->populate( *imageProperties );
    }

    mType = FileType::CART;
    mCart = std::move( pCart );
    return;
  }
  else if ( auto pBS93 = ImageBS93::create( data ) )
  {
    mType = FileType::BS93;
    mBS93 = std::move( pBS93 );
    return;
  }
}

bool InputFile::valid() const
{
  return mType != FileType::UNKNOWN;
}

InputFile::FileType InputFile::getType() const
{
  return mType;
}

std::shared_ptr<ImageBS93 const> InputFile::getBS93() const
{
  return mBS93;
}

std::shared_ptr<ImageCart const> InputFile::getCart() const
{
  return mCart;
}



