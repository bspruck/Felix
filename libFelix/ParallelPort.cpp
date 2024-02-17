#include "ParallelPort.hpp"
#include "Cartridge.hpp"
#include "ComLynx.hpp"
#include "Core.hpp"


ParallelPort::ParallelPort( Core & core, ComLynx & comLynx, RestProvider const & restProvider ) : mCore{ core }, mComLynx{ comLynx }, mRestProvider{ restProvider },
  mOutputMask{}, mData{}
{
}

void ParallelPort::setDirection( uint8_t value )
{
  mOutputMask = value;
}

uint8_t ParallelPort::getDirection() const
{
  return mOutputMask & 0x1f;
}

void ParallelPort::setData( uint8_t value )
{
  mData = value;

  if ( ( mOutputMask & Mask::CART_ADDR_DATA ) != 0 )
  {
    mCore.getCartridge().setCartAddressData( ( mData & Mask::CART_ADDR_DATA ) != 0 );
    mCore.getCartridge().setPower( ( mData & Mask::CART_ADDR_DATA ) == 0 );
  }

  if ( ( mOutputMask & Mask::AUDIN ) != 0 )
  {
    mCore.getCartridge().setAudIn( ( mData & Mask::AUDIN ) != 0 );
  }
}

uint8_t ParallelPort::getData( uint64_t tick ) const
{
  uint8_t result{};

  if ( ( mOutputMask & Mask::AUDIN ) == 0 )
  {
    result |= mCore.getCartridge().getAudIn( tick ) ? Mask::AUDIN : 0;
  }
  else
  {
    result |= mData & Mask::AUDIN;
  }

  if ( ( mOutputMask & Mask::RESTLESS ) == 0 )
  {
    result |= ( ( mData & Mask::RESTLESS ) == 0 ) && !mRestProvider.rest() ? Mask::RESTLESS : 0;
  }
  else
  {
    result |= mData & Mask::RESTLESS;
  }

  if ( ( mOutputMask & Mask::NOEXP ) == 0 )
  {
    result |=mComLynx.present() ? Mask::NOEXP : 0;
  }
  else
  {
    result |= mData & Mask::NOEXP;
  }

  if ( ( mOutputMask & Mask::CART_ADDR_DATA ) != 0 )
  {
    result |= mData & Mask::CART_ADDR_DATA;
  }

  if ( ( mOutputMask & Mask::EXTERNAL_POWER ) == 0 )
  {
    result |= Mask::EXTERNAL_POWER;
  }
  else
  {
    result |= mData & Mask::EXTERNAL_POWER;
  }

  return result;
}
