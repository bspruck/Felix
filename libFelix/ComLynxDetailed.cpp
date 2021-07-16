#include "pch.hpp"
#include "ComLynxDetailed.hpp"
#include "Utility.hpp"
#include "ComLynxWire.hpp"
#include "Log.hpp"

ComLynxDetailed::ComLynxDetailed( std::shared_ptr<ComLynxWire> comLynxWire ) : mId{ comLynxWire->connect() }, mTx{ mId, comLynxWire }, mRx{ mId, comLynxWire }
{
}

ComLynxDetailed::~ComLynxDetailed()
{
}

bool ComLynxDetailed::pulse()
{
  mTx.process();
  mRx.process();

  return mRx.interrupt() || mTx.interrupt();
}

void ComLynxDetailed::setCtrl( uint8_t value )
{
  mTx.setCtrl( value );
  mRx.setCtrl( value );
}

void ComLynxDetailed::setData( uint8_t data )
{
  mTx.setData( data );
}

uint8_t ComLynxDetailed::getCtrl() const
{
  uint8_t status = mTx.getStatus() | mRx.getStatus();

  L_DEBUG << "TxRx" << mId << ": "
    << ( ( status & SERCTL::TXRDY ) ? "TXRDY " : " " )
    << ( ( status & SERCTL::RXRDY ) ? "RXRDY " : " " )
    << ( ( status & SERCTL::TXEMPTY ) ? "TXEMPTY " : " " )
    << ( ( status & SERCTL::PARERR ) ? "PARERR " : " " )
    << ( ( status & SERCTL::OVERRUN ) ? "OVERRUN " : " " )
    << ( ( status & SERCTL::FRAMERR ) ? "FRAMERR " : " " )
    << ( ( status & SERCTL::RXBRK ) ? "RXBRK " : " " )
    << ( ( status & SERCTL::PARBIT ) ? "PARBIT " : " " );

  return mTx.getStatus() | mRx.getStatus();
}

uint8_t ComLynxDetailed::getData()
{
  return mRx.getData();
}

bool ComLynxDetailed::interrupt() const
{
  bool rx = mRx.interrupt();
  bool tx = mTx.interrupt();
  bool res = rx || tx;

  if ( res )
  {
    L_DEBUG << "TxRx" << mId << ": Int "
      << ( rx ? "Rx " : " " ) << ( tx ? "Tx " : " " );
    return true;
  }
  else
  {
    return false;
  }
}

bool ComLynxDetailed::present() const
{
  return true;
}

ComLynxDetailed::Transmitter::Transmitter( int id, std::shared_ptr<ComLynxWire> comLynxWire ) : mWire{ std::move( comLynxWire ) }, mData{}, mState{ 1 }, mCounter{}, mParity{}, mShifter{}, mParEn{}, mIntEn{}, mTxBrk{}, mParBit{}, mId{ id }
{
}

void ComLynxDetailed::Transmitter::setCtrl( uint8_t ctrl )
{
  mIntEn = ctrl & SERCTL::TXINTEN;
  mParEn = ( ctrl & SERCTL::PAREN ) ? 1 : 0;
  mParBit = ctrl & SERCTL::PARBIT;
  mTxBrk = ctrl & SERCTL::TXBRK;

  L_DEBUG << "Tx" << mId << ": IntEn=" << ( mIntEn ? 1 : 0 ) << " ParEn=" << ( mParEn ? 1 : 0 ) << " ParBit=" << mParity << " TxBrk=" << ( mTxBrk ? 1 : 0 );
}

void ComLynxDetailed::Transmitter::setData( int data )
{
  mData = data;
  L_DEBUG << "Tx" << mId << ": Data=" << std::hex << std::setw( 2 ) << std::setfill( '0' ) << mData.value();
}

uint8_t ComLynxDetailed::Transmitter::getStatus() const
{
  return
    ( !mData.has_value()  ? SERCTL::TXRDY   : 0 ) |
    ( ( mCounter == 0 )   ? SERCTL::TXEMPTY : 0 );
}

bool ComLynxDetailed::Transmitter::interrupt() const
{
  return !mData.has_value() && mIntEn != 0;
}

void ComLynxDetailed::Transmitter::process()
{
  switch ( mCounter )
  {
  case 2:
    if ( mParEn )
    {
      L_TRACE << "Tx" << mId << ": Parity=" << mParity;
      pull( mParity );
    }
    else
    {
      L_TRACE << "Tx" << mId << ": ParBit=" << mParBit;
      pull( mParBit );
    }
    mCounter = 1;
    break;
  case 1:
    pull( 1 );
    mCounter = 0;
    L_DEBUG << "Tx" << mId << ": Stop";
    break;
  case 0:
    if ( mTxBrk )
    {
      L_TRACE << "Tx" << mId << ": Brk";
      pull( 0 );
    }
    else if ( mData )
    {
      pull( 0 );
      mShifter = mData.value();
      mData.reset();
      mCounter = 10;
      mParity = 0;
      L_INFO << "Tx" << mId << ": Start Data=" << std::hex << std::setw( 2 ) << std::setfill( '0' ) << mShifter;
    }
    //else
    //{
    //  L_TRACE << "Tx" << mId << " nop";
    //}
    break;
  default:
    L_TRACE << "Tx" << mId << ": #" << ( 10 - mCounter ) << "=" << ( mShifter & 1 );
    pull( mShifter & 1 );
    mParity ^= mShifter & 1;
    mShifter >>= 1;
    mCounter -= 1;
    break;
  }
}

void ComLynxDetailed::Transmitter::pull( int bit )
{
  if ( mState != bit )
  {
    mState = bit;
    if ( mState )
    {
      mWire->pullUp();
    }
    else
    {
      mWire->pullDown();
    }
  }
}

ComLynxDetailed::Receiver::Receiver( int id, std::shared_ptr<ComLynxWire> comLynxWire ) : mWire{ std::move( comLynxWire ) }, mData{}, mCounter{}, mParity{}, mShifter{}, mParErr{}, mFrameErr{}, mRxBrk{}, mOverrun{}, mIntEn{}, mId{ id }
{
}

void ComLynxDetailed::Receiver::setCtrl( uint8_t ctrl )
{
  mIntEn = ctrl & SERCTL::RXINTEN;
  if ( ctrl & SERCTL::RESETERR )
  {
    mParErr = 0;
    mFrameErr = 0;
    mRxBrk = 0;
    mOverrun = 0;
    mRxBrk = 0;
  }

  L_DEBUG << "Rx" << mId << ": IntEn=" << ( mIntEn ? 1 : 0 ) << ( (ctrl & SERCTL::RESETERR ) ? " ResetErr" : "" );
}

int ComLynxDetailed::Receiver::getData()
{
  if ( mData.has_value() )
  {
    L_DEBUG << "Rx" << mId << ": Data=" << std::hex << std::setw( 2 ) << std::setfill( '0' ) << mData.value();
    int result = mData.value_or( 0 );
    mData.reset();
    return result;
  }
  else
  {
    L_DEBUG << "Rx" << mId << ": Data=nil";
    return 0;
  }
}

uint8_t ComLynxDetailed::Receiver::getStatus() const
{
  return
    ( mData.has_value() ? SERCTL::RXRDY : 0 ) |
    mParErr |
    mOverrun |
    mFrameErr |
    mRxBrk |
    mParity;
}

bool ComLynxDetailed::Receiver::interrupt() const
{
  return mData.has_value() && mIntEn != 0;
}

void ComLynxDetailed::Receiver::process()
{
  switch ( mCounter )
  {
  case 10:
  case 9:
  case 8:
  case 7:
  case 6:
  case 5:
  case 4:
  case 3:
    mShifter |= ( mWire->value() << 8 );
    mShifter >>= 1;
    mParity ^= mWire->value();
    mCounter -= 1;
    L_TRACE << "Rx" << mId << ": #" << ( 9 - mCounter ) << "=" << ( mShifter & 1 );
    break;
  case 2:
    mParErr |= ( ( mParity & 1 ) != mWire->value() ) ? SERCTL::PARERR : 0;
    L_TRACE << "Rx" << mId << ": Parity=" << mParity << " ParBit=" << mWire->value() << " ParErr=" << (  mParErr ? 1 : 0 );
    mCounter = 1;
    break;
  case 1:
    if ( mWire->value() )
    {
      mOverrun |= mData.has_value() ? SERCTL::OVERRUN : 0;
      mData = mShifter;
      mCounter = 0;
      L_INFO << "Rx" << mId << ": Stop Data=" << std::hex << std::setw( 2 ) << std::setfill( '0' ) << mShifter << ( mOverrun ? " Overrun" : "" );
    }
    else
    {
      mFrameErr |= SERCTL::FRAMERR;
      mCounter = 11;
      L_DEBUG << "Rx" << mId << ": FrameErr";
    }
    break;
  case 0:
    if ( mWire->value() == 0 )
    {
      L_DEBUG << "Rx" << mId << ": Start";
      mCounter = 10;
      mParity = 0;
      mShifter = 0;
    }
    //else
    //{
    //  L_TRACE << "Rx" << mId << " nop";
    //}
    break;
  default:
    if ( mWire->value() == 0 )
    {
      if ( mCounter++ >= 24 )
      {
        mRxBrk = SERCTL::RXBRK;
        L_TRACE << "Rx" << mId << ": RxBrk=" << mCounter;
      }
    }
    else
    {
      L_TRACE << "Rx" << mId << ": Brk pullup";
      mCounter = 0;
    }
    break;
  }
}