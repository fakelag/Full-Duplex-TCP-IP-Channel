#pragma once

#include "Protocol.h"
#include "BitBuf.h"

/* SVC notification states */
#define SV_CLIENTCONNECT		0
#define SV_CLIENTDISCONNECT		1
#define SV_CLIENTSHUTDOWN		2

enum net_channel_flags_t
{
	NET_DISCONNECT_BY_HOST		= ( 1 << 0 ),
	NET_DISCONNECT_BY_PROTOCOL	= ( 1 << 1 )
};

class INetChannel;
class CNETHandlerMessage;

typedef bool ( *ServerConnectionNotifyFn )( INetChannel* pNetChannel, int nState );
typedef void( *OnHandlerMessageReceivedFn )( INetChannel* pNetChannel, CNETHandlerMessage* pNetMessage );

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
	~INetMessage() {};

	virtual int				Serialize( void* pBuf, unsigned long nSize ) = 0;
	virtual bool			DeSerialize( void* pBuf, unsigned long nSize ) = 0;
	virtual void			ProcessMessage() = 0;

	virtual int				GetType() const = 0;
	
	void*					CreateManifest( void* pBuf, unsigned long nSize ) const
	{
		if ( nSize < sizeof( long ) )
			return 0;

		( ( long* ) pBuf )[ 0 ] = GetType();
		return static_cast< void* >( ( char* ) pBuf + sizeof( long ) );
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
	~INetChannel() {};
	virtual bool			Connect( const char* pszHost, int nPort ) = 0;
	virtual void			Disconnect( const char* pszReason ) = 0;
	virtual void			Transmit() = 0;
	virtual bool			Reconnect() = 0;

	virtual void			SendNetMessage( INetMessage* pNetMessage ) = 0;
	virtual void			SetTickRate( long nTickRate ) = 0;
	virtual void			SetMessageHandler( OnHandlerMessageReceivedFn pfnHandler ) = 0;

	virtual bool			IsConnected()					const = 0;
	virtual const char*		GetDisconnectReason()			const = 0;
	virtual const char*		GetHostIPString()				const = 0;
	virtual unsigned long	GetHostIP()						const = 0;
	virtual unsigned long	GetFlags()						const = 0;
	virtual int				GetOutgoingSequenceNr()			const = 0;
	virtual int				GetIncomingSequenceNr()			const = 0;
	virtual int				GetTickRate()					const = 0;
	virtual unsigned int	GetSocket()						const = 0;

protected:
	friend class CNETHandlerMessage;
	friend class CNETDisconnect;
	virtual void			ProcessHandlerMessage( CNETHandlerMessage* pNetMessage ) = 0;
	virtual void			DisconnectInternal( const char* pszReason ) = 0;
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
	int						m_nSequenceNr;
};

class CNETDisconnect : public INetMessage
{
public:
	CNETDisconnect( INetChannel* pNetChannel );
	CNETDisconnect( INetChannel* pNetChannel, const char* pszReason );

	int						Serialize( void* pBuf, unsigned long nSize );
	bool					DeSerialize( void* pBuf, unsigned long nSize );
	void					ProcessMessage();

	int						GetType() const { return net_Disconnect; }

private:
	char					m_szDisconnectReason[ 128 ];
};

class CNETHandlerMessage : public INetMessage
{
public:
	CNETHandlerMessage( INetChannel* pNetChannel ) : INetMessage( pNetChannel )
	{
		m_Write.Reset();
		m_Read.Reset();
	}

	int						Serialize( void* pBuf, unsigned long nSize );
	bool					DeSerialize( void* pBuf, unsigned long nSize );
	void					ProcessMessage();

	int						GetType() const { return net_HandlerMsg; }

	bf_write&				GetWrite() { return m_Write; }
	bf_read&				GetRead() { return m_Read; }

	bf_write&				InitWriter()
	{
		m_Write.Init( m_Data, NET_BUFFER_SIZE );
		return m_Write;
	}

	bf_read& InitReader()
	{
		m_Read.Init( m_Data, NET_BUFFER_SIZE );
		return m_Read;
	}

private:
	char					m_Data[ NET_BUFFER_SIZE ];
	bf_write				m_Write;
	bf_read					m_Read;
};


class CCLCConnect : public INetMessage
{
public:
	CCLCConnect( INetChannel* pNetChannel ) : INetMessage( pNetChannel )
	{
		m_Msg = '\0';
	}

	int						Serialize( void* pBuf, unsigned long nSize );
	bool					DeSerialize( void* pBuf, unsigned long nSize );
	void					ProcessMessage();

	int						GetType() const { return clc_Connect; }

private:
	char					m_Msg;
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
bool					NET_ProcessListenSocket( const char* pszPort, int nTickRate, ServerConnectionNotifyFn pfnNotify );