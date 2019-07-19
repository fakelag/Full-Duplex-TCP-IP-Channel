#pragma once

#include "Protocol.h"
#include "BitBuf.h"

#include "winsock2.h"
#include "ws2tcpip.h"

/* SVC Notification States */
#define SV_CLIENTCONNECT		0
#define SV_CLIENTDISCONNECT		1

enum net_channel_flags_t
{
	NET_DISCONNECT_BY_HOST		= ( 1 << 0 ),
	NET_DISCONNECT_BY_PROTOCOL	= ( 1 << 1 )
};

class INetChannel;
class INetMessage;
class INetIntermediateContext;

class CNETHandlerMessage;
class CCLCConnect;

typedef void( *ServerRunFrameFn )();
typedef bool ( *ServerConnectionNotifyFn )( INetChannel* pNetChannel, int nState );
typedef void( *OnHandlerMessageReceivedFn )( INetChannel* pNetChannel, INetMessage* pNetMessage );
typedef void( *OnDataTransmissionProgressFn )( const void* pProps, long nPropsLength, long nBytesReceived, long nBytesTotal );

class CCriticalSectionAutolock
{
public:
	CCriticalSectionAutolock( void* hLock );
	~CCriticalSectionAutolock();

private:
	void* m_hLock;
};

#define CRITICAL_SECTION_AUTOLOCK( hLock ) \
CCriticalSectionAutolock CSAutoLock__##hLock##( &hLock );

#define CRITICAL_SECTION_START( hLock ) \
EnterCriticalSection( hLock );

#define CRITICAL_SECTION_END( hLock ) \
LeaveCriticalSection( hLock );


class INetMessage
{
public:
	INetMessage( INetChannel* pNetChannel );
	virtual ~INetMessage() {};

	virtual int				Serialize( void* pBuf, unsigned long nSize ) = 0;
	virtual bool			DeSerialize( void* pBuf, unsigned long nSize ) = 0;
	virtual void			ProcessMessage() = 0;

	virtual int				GetType() const = 0;
	
	void*					CreateManifest( void* pBuf, unsigned long nSize ) const
	{
		if ( nSize < PACKET_MANIFEST_SIZE )
			return 0;

		( ( long* ) pBuf )[ 0 ] = GetType();
		return static_cast< void* >( ( char* ) pBuf + PACKET_MANIFEST_SIZE );
	}

	INetChannel*			GetChannel()
	{
		return m_pNetChannel;
	}

private:
	INetChannel*			m_pNetChannel;
};

class INetChannel
{
public:
	virtual ~INetChannel() {};
	virtual bool			Connect( unsigned int hSocket ) = 0;
	virtual bool			Connect( const char* pszHost, int nPort ) = 0;
	virtual void			Disconnect( const char* pszReason ) = 0;
	virtual void			Transmit( INetMessage* pNetMessage = NULL ) = 0;
	virtual bool			Reconnect() = 0;

	virtual void			SendNetMessage( INetMessage* pNetMessage ) = 0;
	virtual void			SendNetData( char* pData, long nSize, const bf_write* pProps ) = 0;
	virtual void			SetMessageHandler( OnHandlerMessageReceivedFn pfnHandler ) = 0;
	virtual void			SetTransmissionProxy( OnDataTransmissionProgressFn pfnProxy ) = 0;
	virtual void			SetIntermediateProxy( INetIntermediateContext* pContext ) = 0;
	virtual void			SetTickRate( long nTickRate ) = 0;
	virtual void			SetOutgoingSequenceNr( long nSeq )		= 0;
	virtual void			SetIncomingSequenceNr( long nSeq )		= 0;

	virtual bool			IsConnected()							const = 0;
	virtual bool			IsSending()								const = 0;
	virtual bool			IsReceiving()							const = 0;
	virtual bool			IsActiveTransmission()					const = 0;
	virtual bool			IsActiveSocket()						const = 0;
	virtual const char*		GetDisconnectReason()					const = 0;
	virtual const char*		GetHostIPString()						const = 0;
	virtual unsigned long	GetHostIP()								const = 0;
	virtual unsigned long	GetFlags()								const = 0;
	virtual long			GetOutgoingSequenceNr()					const = 0;
	virtual long			GetIncomingSequenceNr()					const = 0;
	virtual long			GetTransferSequenceNr()					const = 0;
	virtual int				GetTickRate()							const = 0;
	virtual unsigned int	GetSocket()								const = 0;

protected:
	friend class CNETHandlerMessage;
	friend class CNETDisconnect;

	virtual void			ProcessHandlerMessage( CNETHandlerMessage* pNetMessage ) = 0;
	virtual void			DisconnectInternal( CNETDisconnect* pNetDisconnect ) = 0;
};

class INetIntermediateContext
{
public:
	virtual ~INetIntermediateContext() {};
	virtual void			ProcessOutgoing( char* pData, long nLength ) = 0;
	virtual void			ProcessIncoming( char* pData, long nLength ) = 0;
};

/* Networked messages derived from INetMessage */

class CNETPing : public INetMessage
{
public:
	CNETPing( INetChannel* pNetChannel ) : INetMessage( pNetChannel )
	{
		m_nSequenceNr = 0;
	}

	int						Serialize( void* pBuf, unsigned long nSize );
	bool					DeSerialize( void* pBuf, unsigned long nSize );
	void					ProcessMessage();

	int						GetType() const { return net_Ping; }

private:
	long					m_nSequenceNr;
};

class CNETDisconnect : public INetMessage
{
public:
	CNETDisconnect( INetChannel* pNetChannel );
	CNETDisconnect( INetChannel* pNetChannel, const char* pszReason );

	int						Serialize( void* pBuf, unsigned long nSize );
	bool					DeSerialize( void* pBuf, unsigned long nSize );
	void					ProcessMessage();

	int						GetType()					const { return net_Disconnect; }
	const char*				GetDisconnectReason()		const { return m_szDisconnectReason; }

private:
	char					m_szDisconnectReason[ 128 ];
};

class CNETDataTransmission : public INetMessage
{
public:
	CNETDataTransmission( INetChannel* pNetChannel ) : INetMessage( pNetChannel )
	{
		m_nId			= -1;
		m_nLength		= 0;
		m_nPropsLength	= 0;
		m_pData			= NULL;

		m_ReadProps.Init( NULL, 0 );
		m_WriteProps.Init( NULL, 0 );
	}

	int						Serialize( void* pBuf, unsigned long nSize );
	bool					DeSerialize( void* pBuf, unsigned long nSize );
	long					GetHeaderPacketSize();
	void					ProcessMessage();

	int						GetType()							const { return net_Transfer; }
	bool					GetTransmissionHasProps()			const { return ( m_nPropsLength > 0 ); }
	long					GetTransmissionLength()				const { return m_nLength; }
	long					GetTransmissionId()					const { return m_nId; }
	char*					GetTransmissionData()				{ return m_pData; }
	void					SetTransmissionId( int nId )		{ m_nId = nId; }

	bool					Init( char* pData, long nLength )
	{
		m_pData = pData;
		m_nLength = nLength;
		return true;
	}

	bool				WriteProps( char* pData, long nLength )
	{
		if ( nLength > NET_PAYLOAD_SIZE )
			return false;

		m_WriteProps.Init( m_Props, nLength );
		m_WriteProps.WriteBytes( pData, nLength );
		return true;
	}

	bf_read&				ReadProps()
	{
		m_ReadProps.Init( m_Props, m_nPropsLength );
		return m_ReadProps;
	}

private:
	long					m_nId;
	long					m_nLength;

	char*					m_pData;

	char					m_Props[ NET_PAYLOAD_SIZE ];
	long					m_nPropsLength;

	bf_write				m_WriteProps;
	bf_read					m_ReadProps;
};

class CNETHandlerMessage : public INetMessage
{
public:
	CNETHandlerMessage( INetChannel* pNetChannel ) : INetMessage( pNetChannel )
	{
		m_nLength = NET_PAYLOAD_SIZE;

		m_Read.Init( NULL, 0 );
		m_Write.Init( NULL, 0 );
	}

	int						Serialize( void* pBuf, unsigned long nSize );
	bool					DeSerialize( void* pBuf, unsigned long nSize );
	void					ProcessMessage();

	int						GetType() const { return net_HandlerMsg; }

	bf_write&				GetWrite()
	{
		m_Write.Init( m_Data, m_nLength );
		return m_Write;
	}

	bf_read&				GetRead()
	{
		m_Read.Init( m_Data, m_nLength );
		return m_Read;
	}

public:
	char					m_Data[ NET_PAYLOAD_SIZE ];
	long					m_nLength;
	bf_write				m_Write;
	bf_read					m_Read;
};


class CCLCConnect : public INetMessage
{
public:
	CCLCConnect( INetChannel* pNetChannel ) : INetMessage( pNetChannel )
	{
		m_ProtocolHeader = NET_PROTOCOL_VERSION ^ NET_PROTOCOL_MASK;
		m_ProtocolUid = NET_PROTOCOL_UID;
	}

	int						Serialize( void* pBuf, unsigned long nSize );
	bool					DeSerialize( void* pBuf, unsigned long nSize );
	void					ProcessMessage();

	int						GetType()					const { return clc_Connect; }
	long					GetProtocolVersion()		const { return m_ProtocolHeader; }
	long					GetProtocolUid()			const { return m_ProtocolUid; }

private:
	long					m_ProtocolHeader;
	long					m_ProtocolUid;
};

class CSVCConnect : public INetMessage
{
public:
	CSVCConnect( INetChannel* pNetChannel ) : INetMessage( pNetChannel )
	{
		m_nTickrate = NET_TICKRATE_DEFAULT;
	}

	int						Serialize( void* pBuf, unsigned long nSize );
	bool					DeSerialize( void* pBuf, unsigned long nSize );
	void					ProcessMessage();

	int						GetType() const { return svc_Connect; }

private:
	long					m_nTickrate;
};

bool					NET_StartUp();
void					NET_Shutdown();
INetChannel*			NET_CreateChannel();
void					NET_DestroyChannel( INetChannel* pNetChannel );
bool					NET_ProcessListenSocket( const char* pszPort, int nTickRate, ServerRunFrameFn pfnPerFrame, ServerConnectionNotifyFn pfnNotify, INetIntermediateContext* pCtx = NULL );