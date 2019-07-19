#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "windows.h"
#include "ws2tcpip.h"
#include "vector"

#include "Inc\Channel.h"

#pragma comment( lib, "Ws2_32.lib" )

class CBaseNetChannel;

DWORD WINAPI NET_ProcessSocket( LPVOID lp );

#ifdef NET_NOTIFY_THREADLOCK
CRITICAL_SECTION g_hNotificationLock;
#endif

class CNetMessageQueue
{
public:
	CNetMessageQueue();
	~CNetMessageQueue();
	void							AddMessage( INetMessage* pNetMessage );
	void							ProcessMessages();

	void							ReleaseQueue();

	int								GetMessageCount()				const { return m_Queue.size(); }
	INetMessage*					GetMessageByIndex( int nMsg )	const { return m_Queue[ nMsg ]; }

	void							LockQueue()
	{
		EnterCriticalSection( &m_hQueueLock );
	}

	void							UnLockQueue()
	{
		LeaveCriticalSection( &m_hQueueLock );
	}

private:
	std::vector< INetMessage* >		m_Queue;
	CRITICAL_SECTION				m_hQueueLock;
};

class CBaseNetChannel : public INetChannel
{
public:
	CBaseNetChannel();
	~CBaseNetChannel();

	bool				Connect( const char* pszHost, int nPort );
	bool				InitFromSocket( SOCKET hSocket, ServerConnectionNotifyFn pfnNotify );

	void				CloseConnection();

	void				SendNetMessage( INetMessage* pNetMessage );
	void				Disconnect( const char* pszReason );
	bool				Reconnect();
	void				Transmit();

	bool				ProcessSocket();

	bool				WaitForTransmit()					const;
	int					GetTickRate()						const { return m_nTickRate; }
	bool				IsConnected()						const { return ( m_hSocket != INVALID_SOCKET ); }
	bool				IsActiveSocket()					const { return ( IsConnected() || m_hNetworkThread != INVALID_HANDLE_VALUE ); }
	const char*			GetDisconnectReason()				const { return m_szDisconnectReason; }
	const char*			GetHostIPString()					const { return m_szHostIP; }
	unsigned long		GetHostIP()							const { return m_nHostIP; }
	unsigned long		GetFlags()							const { return m_nFlags; }
	int					GetOutgoingSequenceNr()				const { return m_nOutgoingSequenceNr; }
	int					GetIncomingSequenceNr()				const { return m_nIncomingSequenceNr; }
	unsigned int		GetSocket()							const { return m_hSocket; }
	void				SetTickRate( long nTickRate )		{ m_nTickRate = ( int ) nTickRate; }

	void				SetMessageHandler( OnHandlerMessageReceivedFn pfnHandler )
	{
		m_MessageHandler = pfnHandler;
	}

protected:
	long				ProcessIncoming();
	long				ProcessOutgoing();

	void				DisconnectInternal( const char* pszReason );
	void				ProcessHandlerMessage( CNETHandlerMessage* pNetMessage );
	long				ProcessPacketHeader( void* pBuf, unsigned long nSize, int* pType );
	long				SendInternal( void* pBuf, unsigned long nSize );
	long				RecvInternal( void* pBuf, unsigned long nSize );

	bool				m_bIsServer;
	int					m_nTickRate;
	int					m_nTimeout;
	int					m_nLastPingCycle;
	long				m_nOutgoingSequenceNr;
	long				m_nIncomingSequenceNr;

	/* Sockets */
	SOCKET				m_hSocket;
	HANDLE				m_hNetworkThread;

	CRITICAL_SECTION	m_hSendLock;
	CRITICAL_SECTION	m_hRecvLock;

	CNetMessageQueue	m_RecvQueue;
	CNetMessageQueue	m_SendQueue;

	unsigned long		m_nFlags;

	char				m_szHostIP[ 32 ];
	unsigned long		m_nHostIP;
	sockaddr_in			m_SockAddr;

	/* Reconnection data */
	const char*			m_szLastHostIP;
	int					m_nLastHostPort;
	bool				m_bCanReconnect;

	/* Server Reserved */
	ServerConnectionNotifyFn	m_pfnNotify;
	char						m_szDisconnectReason[ 128 ];

	/* Handlers */
	OnHandlerMessageReceivedFn	m_MessageHandler;
};

CCriticalSectionAutolock::CCriticalSectionAutolock( void* hLock )
{
	m_hLock = hLock;
	EnterCriticalSection( ( LPCRITICAL_SECTION ) m_hLock );
}

CCriticalSectionAutolock::~CCriticalSectionAutolock()
{
	LeaveCriticalSection( ( LPCRITICAL_SECTION ) m_hLock );
}

CNetMessageQueue::CNetMessageQueue()
{
	InitializeCriticalSection( &m_hQueueLock );
}

CNetMessageQueue::~CNetMessageQueue()
{
	DeleteCriticalSection( &m_hQueueLock );
}

CBaseNetChannel::CBaseNetChannel()
{
	m_bIsServer = false;
	m_bCanReconnect = false;
	m_nOutgoingSequenceNr = 0;
	m_nIncomingSequenceNr = 0;
	m_nLastPingCycle = 0;
	m_hSocket = INVALID_SOCKET;
	m_pfnNotify = NULL;
	m_MessageHandler = NULL;
	m_hNetworkThread = INVALID_HANDLE_VALUE;
	m_nTickRate = 32;
	m_nTimeout = 20000;

	strncpy( m_szDisconnectReason, "Connection lost", sizeof( m_szDisconnectReason ) );

	InitializeCriticalSection( &m_hSendLock );
	InitializeCriticalSection( &m_hRecvLock );
}

CBaseNetChannel::~CBaseNetChannel()
{
	DeleteCriticalSection( &m_hSendLock );
	DeleteCriticalSection( &m_hRecvLock );
}

bool CBaseNetChannel::Connect( const char* pszHost, int nPort )
{
	m_nFlags = 0;
	m_nLastPingCycle = 0;
	m_bIsServer = false;
	strncpy( m_szHostIP, pszHost, sizeof( m_szHostIP ) );

	in_addr addr_info;
	inet_pton( AF_INET, pszHost, &addr_info );
	m_nHostIP = addr_info.s_addr;

	m_hSocket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

	BOOL nState = 1;
	setsockopt( m_hSocket, IPPROTO_TCP, TCP_NODELAY, ( char * ) &nState, sizeof( nState ) );
	setsockopt( m_hSocket, SOL_SOCKET, SO_SNDTIMEO, ( char * ) &m_nTimeout, sizeof( m_nTimeout ) );
	setsockopt( m_hSocket, SOL_SOCKET, SO_RCVTIMEO, ( char * ) &m_nTimeout, sizeof( m_nTimeout ) );

	m_SockAddr.sin_family			= AF_INET;
	m_SockAddr.sin_addr.s_addr		= m_nHostIP;
	m_SockAddr.sin_port				= htons( nPort );

	if ( connect( m_hSocket, ( sockaddr* ) &m_SockAddr, sizeof( m_SockAddr ) ) )
	{
		m_hSocket = INVALID_SOCKET;
		return false;
	}

	m_szLastHostIP = pszHost;
	m_nLastHostPort = nPort;
	m_bCanReconnect = true;

	m_hNetworkThread = CreateThread( NULL, NULL, &NET_ProcessSocket, this, NULL, NULL );

	return true;
}

bool CBaseNetChannel::InitFromSocket( SOCKET hSocket, ServerConnectionNotifyFn pfnNotify )
{
	m_nFlags = 0;
	m_nLastPingCycle = 0;
	m_bIsServer = true;
	m_hSocket = hSocket;

	BOOL nState = 1;
	setsockopt( m_hSocket, IPPROTO_TCP, TCP_NODELAY, ( char * ) &nState, sizeof( nState ) );
	setsockopt( m_hSocket, SOL_SOCKET, SO_SNDTIMEO, ( char * ) &m_nTimeout, sizeof( m_nTimeout ) );
	setsockopt( m_hSocket, SOL_SOCKET, SO_RCVTIMEO, ( char * ) &m_nTimeout, sizeof( m_nTimeout ) );

	int nAddrSize = sizeof( m_SockAddr );
	getsockname( hSocket, ( sockaddr* ) &m_SockAddr, &nAddrSize );

	inet_ntop( AF_INET, &m_SockAddr.sin_addr, m_szHostIP, sizeof( m_szHostIP ) );
	m_nHostIP = m_SockAddr.sin_addr.s_addr;

	{
#ifdef NET_NOTIFY_THREADLOCK
		CRITICAL_SECTION_AUTOLOCK( g_hNotificationLock );
#endif

		m_pfnNotify = pfnNotify;

		if ( m_pfnNotify && !m_pfnNotify( this, SV_CLIENTCONNECT ) )
		{
			m_hSocket = INVALID_SOCKET;
			return false;
		}
	}

	m_hNetworkThread = CreateThread( NULL, NULL, &NET_ProcessSocket, this, NULL, NULL );

	CSVCConnect* pSVCConnect = new CSVCConnect( this );
	SendNetMessage( pSVCConnect );

	return true;
}

void CBaseNetChannel::CloseConnection()
{
	if ( m_hSocket == INVALID_SOCKET )
		return;

	if ( m_bIsServer && m_pfnNotify )
	{
#ifdef NET_NOTIFY_THREADLOCK
		CRITICAL_SECTION_AUTOLOCK( g_hNotificationLock );
#endif

		m_pfnNotify( this, SV_CLIENTDISCONNECT );
	}

	closesocket( m_hSocket );
	m_hSocket = INVALID_SOCKET;

	if ( m_hNetworkThread != INVALID_HANDLE_VALUE )
	{
		if ( GetThreadId( m_hNetworkThread ) != GetCurrentThreadId() )
		{
			if ( WaitForSingleObject( m_hNetworkThread, 2000 ) == WAIT_TIMEOUT )
				TerminateThread( m_hNetworkThread, 0 );
		}
	}

	m_SendQueue.ReleaseQueue();
	m_RecvQueue.ReleaseQueue();

	m_nIncomingSequenceNr = 0;
	m_nOutgoingSequenceNr = 0;
	m_hNetworkThread = INVALID_HANDLE_VALUE;
}

long CBaseNetChannel::ProcessIncoming()
{
	CRITICAL_SECTION_AUTOLOCK( m_hRecvLock );

	char* pRecv = new char[ NET_BUFFER_SIZE ];

	if ( RecvInternal( pRecv, NET_BUFFER_SIZE ) == -1 )
	{
		delete[] pRecv;
		return -1;
	}

	delete[] pRecv;

	m_RecvQueue.ProcessMessages();
	m_RecvQueue.ReleaseQueue();
	return m_nIncomingSequenceNr;
}

long CBaseNetChannel::ProcessOutgoing()
{
	CRITICAL_SECTION_AUTOLOCK( m_hSendLock );

	m_SendQueue.LockQueue();

	char* pData = new char[ NET_BUFFER_SIZE ];

	if ( !m_bIsServer )
	{
		if ( static_cast< int >( 2.0f * ( float ) ( m_nTickRate ) ) < m_nLastPingCycle )
		{
			CNETPing* pNETPing = new CNETPing( this );
			SendNetMessage( pNETPing );
		}
	}

	bool bTransmissionOK = true;
	int nMsgCount = m_SendQueue.GetMessageCount();
	for ( int i = 0; i < nMsgCount; ++i )
	{
		INetMessage* pNetMessage = m_SendQueue.GetMessageByIndex( i );
		int nSerialized = pNetMessage->Serialize( pData, NET_BUFFER_SIZE );

		if ( nSerialized <= 0 )
			continue;

		if ( SendInternal( pData, nSerialized ) == -1 )
		{
			m_SendQueue.UnLockQueue();
			bTransmissionOK = false;
			break;
		}

		m_nLastPingCycle = 0;
	}

	delete[] pData;

	m_SendQueue.ReleaseQueue();
	m_SendQueue.UnLockQueue();
	return ( bTransmissionOK ? m_nOutgoingSequenceNr : -1 );
}

bool CBaseNetChannel::ProcessSocket()
{
	if ( !IsConnected() )
		return false;

	if ( m_hNetworkThread == INVALID_HANDLE_VALUE )
		return false;

	++m_nLastPingCycle;

	int nIncomingSequenceNr = m_nIncomingSequenceNr;
	int nSequenceNumber = ProcessIncoming();

	if ( nSequenceNumber != -1 && nSequenceNumber == nIncomingSequenceNr )
	{
		/* No packets were received this frame */
		nSequenceNumber = ProcessOutgoing();
	}

	if ( nSequenceNumber == -1 )
	{
		m_nFlags |= NET_DISCONNECT_BY_PROTOCOL;
		return false;
	}

	return true;
}

bool CBaseNetChannel::WaitForTransmit() const
{
	int nLastOutgoingSequenceNr = m_nOutgoingSequenceNr;

	while ( m_nOutgoingSequenceNr == nLastOutgoingSequenceNr && IsConnected() )
		Sleep( 150 );

	return true;
}

void CBaseNetChannel::SendNetMessage( INetMessage* pNetMessage )
{
	m_SendQueue.AddMessage( pNetMessage );
}

void CBaseNetChannel::Disconnect( const char* pszReason )
{
	strncpy( m_szDisconnectReason, pszReason, sizeof( m_szDisconnectReason ) );

	CNETDisconnect* pNETDisconnect = new CNETDisconnect( this, pszReason );
	SendNetMessage( pNETDisconnect );

	Transmit();

	CloseConnection();
}

bool CBaseNetChannel::Reconnect()
{
	if ( IsConnected() )
		return true;

	if ( !m_bCanReconnect )
		return false;
	
	if ( m_hNetworkThread == INVALID_HANDLE_VALUE )
	{
		if ( !m_bIsServer )
			return Connect( m_szLastHostIP, m_nLastHostPort );
	}

	return false;
}

void CBaseNetChannel::Transmit()
{
	if ( GetThreadId( m_hNetworkThread ) == GetCurrentThreadId() )
	{
		ProcessOutgoing();
	}
	else
	{
		WaitForTransmit();
	}
}

void CBaseNetChannel::DisconnectInternal( const char* pszReason )
{
	m_nFlags |= NET_DISCONNECT_BY_HOST;
	strncpy( m_szDisconnectReason, pszReason, sizeof( m_szDisconnectReason ) );
	CloseConnection();
}

long CBaseNetChannel::SendInternal( void* pBuf, unsigned long nSize )
{
	if ( !IsConnected() )
		return -1;

	/* Create Header */
	char* pMsg = new char[ nSize + PACKET_HEADER_LENGTH ];
	( ( long* ) pMsg )[ 0 ] = m_nOutgoingSequenceNr;
	( ( long* ) pMsg )[ 1 ] = nSize;

	memcpy( ( void* ) ( pMsg + PACKET_HEADER_LENGTH ), pBuf, nSize );

	if ( send( m_hSocket, pMsg, nSize + PACKET_HEADER_LENGTH, 0 ) == SOCKET_ERROR )
	{
		delete[] pMsg;
		return -1;
	}

	delete[] pMsg;
	return m_nOutgoingSequenceNr++;
}

long CBaseNetChannel::RecvInternal( void* pBuf, unsigned long nSize )
{
	if ( !IsConnected() )
		return -1;

	/* Check socket state */

	fd_set socket_set;
	FD_ZERO( &socket_set );
	FD_SET( m_hSocket, &socket_set );

	TIMEVAL timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 500;

	switch ( select( 0, &socket_set, NULL, NULL, &timeout ) )
	{
	case 1:
		break;
	case SOCKET_ERROR:
		return -1;
	default:
		return m_nIncomingSequenceNr;
	}

	/* Fetch the recv buffer */

	int nReceived = recv( m_hSocket, ( char* ) pBuf, nSize, 0 );

	if ( nReceived <= 0 )
		return -1;

	long nBytesSerialized = 0, nPacketsSerialized = 0;
	while ( nBytesSerialized < nReceived )
	{
		char* pData = ( char* ) pBuf + nBytesSerialized;

		int nType = -1;
		long nLength = ProcessPacketHeader( pData, nReceived, &nType );

		if ( nLength <= 0 )
			return -1;

		if ( nLength >= nReceived )
			return -1;

		/* DeSerialize */

		char* pMessage = ( char* ) pData + PACKET_HEADER_LENGTH + PACKET_MANIFEST_SIZE;
		INetMessage* pNetMessage = NULL;

		switch ( nType )
		{
		case net_Ping:
		{
			pNetMessage = new CNETPing( this );

			if ( !pNetMessage->DeSerialize( pMessage, nLength ) )
				return -1;

			break;
		}
		case net_Disconnect:
		{
			pNetMessage = new CNETDisconnect( this );

			if ( !pNetMessage->DeSerialize( pMessage, nLength ) )
				return -1;

			break;
		}
		case net_HandlerMsg:
		{
			pNetMessage = new CNETHandlerMessage( this );

			if ( !pNetMessage->DeSerialize( pMessage, nLength ) )
				return -1;

			break;
		}
		default:
		{
			if ( m_bIsServer )
			{
				switch ( nType )
				{
				case clc_Connect:
					pNetMessage = new CCLCConnect( this );

					if ( !pNetMessage->DeSerialize( pMessage, nLength ) )
						return -1;

					break;
				default:
					return -1;
				}
			}
			else
			{
				switch ( nType )
				{
				case svc_Connect:
					pNetMessage = new CSVCConnect( this );

					if ( !pNetMessage->DeSerialize( pMessage, nLength ) )
						return -1;

					break;
				default:
					return -1;
				}
			}

			break;
		}
		}

		if ( pNetMessage )
			m_RecvQueue.AddMessage( pNetMessage );

		nBytesSerialized += nLength + PACKET_HEADER_LENGTH;
		++nPacketsSerialized;
	}

#ifdef _DEBUG
	//printf( "nPacketsSerialized=%i bytes=%i\n", nPacketsSerialized, nBytesSerialized );
#endif

	return m_nIncomingSequenceNr;
}

void CBaseNetChannel::ProcessHandlerMessage( CNETHandlerMessage* pNetMessage )
{
	if ( !m_MessageHandler )
	{
#ifdef _DEBUG
		printf( "Received a handler message from '%s'. No handler bound!\n", GetHostIPString() );
#endif
		return;
	}

	m_MessageHandler( this, pNetMessage );
}

long CBaseNetChannel::ProcessPacketHeader( void* pBuf, unsigned long nSize, int* pType )
{
	if ( nSize < PACKET_HEADER_LENGTH + PACKET_MANIFEST_SIZE )
		return -1;

	long nIncomingAck = ( ( long* ) pBuf )[ 0 ];

#ifdef PACKET_STRICT_VALIDATION
	if ( m_nIncomingSequenceNr > 0 && nIncomingAck <= m_nIncomingSequenceNr )
		return -1;

	if ( m_nIncomingSequenceNr > 0 && m_nIncomingSequenceNr + 1 != nIncomingAck )
		return -1;
#endif

	m_nIncomingSequenceNr = nIncomingAck;

	long nIncomingSize = ( ( long* ) pBuf )[ 1 ];

	if ( pType )
		*pType = ( ( int* ) pBuf )[ 2 ];

	if ( nIncomingSize <= 0 )
		return -1;

	return nIncomingSize;
}

void CNetMessageQueue::AddMessage( INetMessage* pNetMessage )
{
	CRITICAL_SECTION_AUTOLOCK( m_hQueueLock );
	m_Queue.insert( m_Queue.end(), pNetMessage );
}

void CNetMessageQueue::ProcessMessages()
{
	CRITICAL_SECTION_AUTOLOCK( m_hQueueLock );

	if ( !m_Queue.size() )
		return;

	int c = m_Queue.size();

	for ( int i = 0; i < c; ++i )
		m_Queue[ i ]->ProcessMessage();
}

void CNetMessageQueue::ReleaseQueue()
{
	CRITICAL_SECTION_AUTOLOCK( m_hQueueLock );

	int c = m_Queue.size();
	for ( int i = 0; i < c; ++i )
		delete m_Queue[ i ];

	m_Queue.clear();
}

DWORD WINAPI NET_ProcessSocket( LPVOID lp )
{
	CBaseNetChannel* pNetChannel = ( CBaseNetChannel* ) lp;

	while ( pNetChannel->ProcessSocket() )
		Sleep( static_cast< int >( ( 1.0f / ( float ) pNetChannel->GetTickRate() ) * 1000.0f ) );

	pNetChannel->CloseConnection();
	return 0;
}

bool NET_StartUp()
{
	WSADATA wsaData;
	if ( WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) )
		return false;

#ifdef NET_NOTIFY_THREADLOCK
	InitializeCriticalSection( &g_hNotificationLock );
#endif

	return true;
}

void NET_Shutdown()
{
	WSACleanup();

#ifdef NET_NOTIFY_THREADLOCK
	DeleteCriticalSection( &g_hNotificationLock );
#endif
}

INetChannel* NET_CreateChannel()
{
	CBaseNetChannel* pNetChannel = new CBaseNetChannel();
	return pNetChannel;
}

void NET_DestroyChannel( INetChannel* pNetChannel )
{
	static_cast< CBaseNetChannel* >( pNetChannel )->CloseConnection();
	delete pNetChannel;
}

std::vector< CBaseNetChannel* > g_ListenChannels;
bool NET_ProcessListenSocket( const char* pszPort, int nTickRate, ServerConnectionNotifyFn pfnNotify )
{
	addrinfo hints;
	addrinfo* info = NULL;

	ZeroMemory( &hints, sizeof( hints ) );

	hints.ai_family			= AF_INET;
	hints.ai_socktype		= SOCK_STREAM;
	hints.ai_protocol		= IPPROTO_TCP;
	hints.ai_flags			= AI_PASSIVE;

	if ( getaddrinfo( NULL, pszPort, &hints, &info ) )
		return false;

	SOCKET hListenSocket = socket( info->ai_family, info->ai_socktype, info->ai_protocol );
	if ( hListenSocket == INVALID_SOCKET )
	{
		freeaddrinfo( info );
		return false;
	}

	if ( bind( hListenSocket, info->ai_addr, ( int ) info->ai_addrlen ) == SOCKET_ERROR )
	{
		freeaddrinfo( info );
		closesocket( hListenSocket );
		return false;
	}

	freeaddrinfo( info );

	if ( nTickRate == 0 )
		nTickRate = NET_TICKRATE_DEFAULT;

	nTickRate = max( min( nTickRate, NET_TICKRATE_MAX ), NET_TICKRATE_MIN );

	while ( listen( hListenSocket, SOMAXCONN ) != SOCKET_ERROR )
	{
		SOCKET hClient = accept( hListenSocket, NULL, NULL );

		if ( hClient == INVALID_SOCKET )
			continue;

		CBaseNetChannel* pNetChannel = ( CBaseNetChannel* ) NET_CreateChannel();
		pNetChannel->SetTickRate( nTickRate );

		if ( pNetChannel->InitFromSocket( hClient, pfnNotify ) )
			g_ListenChannels.insert( g_ListenChannels.end(), pNetChannel );

		int c = g_ListenChannels.size();
		for ( int i = c - 1; i >= 0; --i )
		{
			if ( !g_ListenChannels[ i ]->IsActiveSocket() )
			{
				NET_DestroyChannel( g_ListenChannels[ i ] );
				g_ListenChannels.erase( g_ListenChannels.begin() + i );
			}
		}
	}

	int c = g_ListenChannels.size();
	for ( int i = c - 1; i >= 0; --i )
	{
		g_ListenChannels[ i ]->CloseConnection();
		NET_DestroyChannel( g_ListenChannels[ i ] );
	}

	g_ListenChannels.clear();
	closesocket( hListenSocket );
	return true;
}