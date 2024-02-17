#include "ComLynx.hpp"
#include "Utility.hpp"
#include "ComLynxWire.hpp"
#include "Log.hpp"

ComLynx::ComLynx( std::shared_ptr<ComLynxWire> comLynxWire ) : mId{ comLynxWire->connect() }, mTx{ mId, comLynxWire }, mRx{ mId, comLynxWire }
{
}

ComLynx::~ComLynx()
{
}

bool ComLynx::pulse()
{
  mTx.process();
  mRx.process();

  return mRx.interrupt() || mTx.interrupt();
}

void ComLynx::setCtrl( uint8_t value )
{
  mTx.setCtrl( value );
  mRx.setCtrl( value );
}

void ComLynx::setData( uint8_t data )
{
  mTx.setData( data );
}

uint8_t ComLynx::getCtrl() const
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

uint8_t ComLynx::getData()
{
  return mRx.getData();
}

bool ComLynx::interrupt() const
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

bool ComLynx::present() const
{
  return true;
}

ComLynx::Transmitter::Transmitter( int id, std::shared_ptr<ComLynxWire> comLynxWire ) : mWire{ std::move( comLynxWire ) }, mData{}, mState{ 1 }, mCounter{}, mParity{}, mShifter{}, mParEn{}, mIntEn{}, mTxBrk{}, mParBit{}, mId{ id }
{
}

void ComLynx::Transmitter::setCtrl( uint8_t ctrl )
{
  mIntEn = ctrl & SERCTL::TXINTEN;
  mParEn = ( ctrl & SERCTL::PAREN ) ? 1 : 0;
  mParBit = ctrl & SERCTL::PARBIT;
  mTxBrk = ctrl & SERCTL::TXBRK;

  L_DEBUG << "Tx" << mId << ": IntEn=" << ( mIntEn ? 1 : 0 ) << " ParEn=" << ( mParEn ? 1 : 0 ) << " ParBit=" << mParity << " TxBrk=" << ( mTxBrk ? 1 : 0 );
}

void ComLynx::Transmitter::setData( int data )
{
  mData = data;
  L_DEBUG << "Tx" << mId << ": SetData=" << std::hex << std::setw( 2 ) << std::setfill( '0' ) << mData.value();
}

uint8_t ComLynx::Transmitter::getStatus() const
{
  return
    ( !mData.has_value()  ? SERCTL::TXRDY   : 0 ) |
    ( ( mCounter == 0 )   ? SERCTL::TXEMPTY : 0 );
}

bool ComLynx::Transmitter::interrupt() const
{
  return !mData.has_value() && mIntEn != 0;
}

void ComLynx::Transmitter::process()
{
  switch ( mCounter )
  {
  case 1:
    pull( 1 );
    mParity = std::popcount( mShifter ) & 1;
    mWire->setCoarse( mShifter, mParEn ? mParity : mParBit );
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
    break;
  default:
    mCounter -= 1;
    break;
  }
}

void ComLynx::Transmitter::pull( int bit )
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

ComLynx::Receiver::Receiver( int id, std::shared_ptr<ComLynxWire> comLynxWire ) : mWire{ std::move( comLynxWire ) }, mData{}, mCounter{}, mParity{}, mParErr{}, mFrameErr{}, mRxBrk{}, mOverrun{}, mIntEn{}, mId{ id }
{
}

void ComLynx::Receiver::setCtrl( uint8_t ctrl )
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

int ComLynx::Receiver::getData()
{
  if ( mData.has_value() )
  {
    L_DEBUG << "Rx" << mId << ": GetData=" << std::hex << std::setw( 2 ) << std::setfill( '0' ) << mData.value();
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

uint8_t ComLynx::Receiver::getStatus() const
{
  return
    ( mData.has_value() ? SERCTL::RXRDY : 0 ) |
    mParErr |
    mOverrun |
    mFrameErr |
    mRxBrk |
    mParity;
}

bool ComLynx::Receiver::interrupt() const
{
  return mData.has_value() && mIntEn != 0;
}

void ComLynx::Receiver::process()
{
  if ( mCounter == 0 )
  {
    if ( mWire->wire() == -1 )
    {
      L_DEBUG << "Rx" << mId << ": Start";
      mCounter = 1;
      mParity = 0;
    }
  }
  else
  {
    switch ( mWire->wire() )
    {
    case 0:
      if ( mCounter > 24 )
      {
        L_TRACE << "Rx" << mId << ": Brk pullup";
      }
      else
      {
        bool overrun = mData.has_value();
        mOverrun |= overrun ? SERCTL::OVERRUN : 0;
        mData = mWire->getCoarse( mParity );
        L_TRACE << "Rx" << mId << ": Stop Data=" << std::hex << std::setw( 2 ) << std::setfill( '0' ) << *mData << ( overrun ? " overrun" : "" );
      }
      mCounter = 0;
      break;
    case -1:
      if ( mCounter > 24 )
      {
        mRxBrk = SERCTL::RXBRK;
        L_TRACE << "Rx" << mId << ": RxBrk=" << mCounter;
      }
      mCounter += 1;
      break;
    default:
      mFrameErr |= SERCTL::FRAMERR;
      L_DEBUG << "Rx" << mId << ": FrameErr";
      mCounter += 1;
      break;
    }
  }
}
