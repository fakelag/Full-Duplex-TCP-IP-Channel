#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "windows.h"
#include "Inc/Channel.h"

#ifdef _DEBUG
#include "iostream"
#endif

INetMessage::INetMessage( INetChannel* pNetChannel )
{
	m_pNetChannel = pNetChannel;
}

int CNETPing::Serialize( void* pBuf, unsigned long nSize )
{
	void* pData = CreateManifest( pBuf, nSize );

	if ( !pData )
		return -1;

	int nSequenceNr = 0;

	INetChannel* pNetChannel = GetChannel();

	if ( pNetChannel )
		nSequenceNr = pNetChannel->GetIncomingSequenceNr();

	( ( int* ) pData )[ 0 ] = nSequenceNr;
	return PACKET_MANIFEST_SIZE + sizeof( int );
}

bool CNETPing::DeSerialize( void* pBuf, unsigned long nSize )
{
	if ( nSize != PACKET_MANIFEST_SIZE + sizeof( int ) )
		return false;

	m_nSequenceNr = *( int* ) pBuf;
	return true;
}

void CNETPing::ProcessMessage()
{
#ifdef _DEBUG
	//printf( "CNETPing::ProcessMessage: nSequenceNr: %i\n", m_nSequenceNr );
#endif
}

CNETDisconnect::CNETDisconnect( INetChannel* pNetChannel ) : INetMessage( pNetChannel )
{
	m_szDisconnectReason[ 0 ] = '\0';
}

CNETDisconnect::CNETDisconnect( INetChannel* pNetChannel, const char* pszReason ) : INetMessage( pNetChannel )
{
	strncpy( m_szDisconnectReason, pszReason, sizeof( m_szDisconnectReason ) );
}

int CNETDisconnect::Serialize( void* pBuf, unsigned long nSize )
{
	void* pData = CreateManifest( pBuf, nSize );

	if ( !pData )
		return -1;

	if ( nSize - sizeof( m_szDisconnectReason ) - PACKET_MANIFEST_SIZE < 0 )
		return -1;

	int nStringLength = strlen( m_szDisconnectReason );
	strncpy( ( char* ) pData, m_szDisconnectReason, nStringLength );

	( ( char* ) pData + nStringLength )[ 0 ] = '\0';
	return PACKET_MANIFEST_SIZE + nStringLength + sizeof( char );
}

bool CNETDisconnect::DeSerialize( void* pBuf, unsigned long nSize )
{
	if ( nSize > PACKET_MANIFEST_SIZE + sizeof( m_szDisconnectReason ) + 4 )
		return false;

	memset( m_szDisconnectReason, 0, sizeof( m_szDisconnectReason ) );
	strncpy( m_szDisconnectReason, ( char* ) pBuf, sizeof( m_szDisconnectReason ) );

	return true;
}

void CNETDisconnect::ProcessMessage()
{
	INetChannel* pNetChannel = GetChannel();

	if ( pNetChannel )
		pNetChannel->DisconnectInternal( m_szDisconnectReason );
}

int CNETHandlerMessage::Serialize( void* pBuf, unsigned long nSize )
{
	void* pData = CreateManifest( pBuf, nSize );

	if ( !pData )
		return -1;

	if ( nSize < m_Write.GetNumBytesWritten() + 4 )
		return -1;

	memcpy( pData, m_Write.GetData(), m_Write.GetNumBytesWritten() );
	return PACKET_MANIFEST_SIZE + m_Write.GetNumBytesWritten();
}

bool CNETHandlerMessage::DeSerialize( void* pBuf, unsigned long nSize )
{
	if ( nSize > PACKET_MANIFEST_SIZE + NET_BUFFER_SIZE )
		return false;

	memcpy( m_Data, pBuf, min( NET_BUFFER_SIZE, nSize ) );
	return true;
}

void CNETHandlerMessage::ProcessMessage()
{
	INetChannel* pNetChannel = GetChannel();

	if( pNetChannel )
		pNetChannel->ProcessHandlerMessage( this );
}

int CCLCConnect::Serialize( void* pBuf, unsigned long nSize )
{
	void* pData = CreateManifest( pBuf, nSize );

	if ( !pData )
		return -1;

	( ( char* ) pData )[ 0 ] = 'h';
	return PACKET_MANIFEST_SIZE + sizeof( char );
}

bool CCLCConnect::DeSerialize( void* pBuf, unsigned long nSize )
{
	if ( nSize != PACKET_MANIFEST_SIZE + sizeof( char ) )
		return false;

	m_Msg = *( char* ) pBuf;

	if ( m_Msg != 'h' )
		return false;

	return true;
}

void CCLCConnect::ProcessMessage()
{

}

int CSVCConnect::Serialize( void* pBuf, unsigned long nSize )
{
	void* pData = CreateManifest( pBuf, nSize );

	if ( !pData )
		return -1;

	INetChannel* pNetChannel = GetChannel();

	if ( pNetChannel )
		m_nTickrate = pNetChannel->GetTickRate();

	( ( long* ) pData )[ 0 ] = m_nTickrate;
	return PACKET_MANIFEST_SIZE + sizeof( long );
}

bool CSVCConnect::DeSerialize( void* pBuf, unsigned long nSize )
{
	if ( nSize != PACKET_MANIFEST_SIZE + sizeof( long ) )
		return false;

	m_nTickrate = *( long* ) pBuf;
	return true;
}

void CSVCConnect::ProcessMessage()
{
	INetChannel* pNetChannel = GetChannel();

	if( pNetChannel )
		pNetChannel->SetTickRate( m_nTickrate );
}