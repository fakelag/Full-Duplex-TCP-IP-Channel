#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "windows.h"
#include "vector"

#include "Inc\Channel.h"

#pragma comment( lib, "Ws2_32.lib" )

class CBaseNetChannel;

bool g_bIsNetInitialized = false;
std::vector< CBaseNetChannel* > g_ListenChannels;
CRITICAL_SECTION g_hListenChannelLock;

#ifdef NET_NOTIFY_THREADLOCK
CRITICAL_SECTION g_hNotificationLock;
#endif

#if (NTDDI_VERSION < NTDDI_VISTA)
int inet_pton( int af, const char *src, void *dst )
{
	struct sockaddr_storage ss;
	int size = sizeof( ss );
	char src_copy[ INET6_ADDRSTRLEN + 1 ];

	ZeroMemory( &ss, sizeof( ss ) );
	/* stupid non-const API */
	strncpy( src_copy, src, INET6_ADDRSTRLEN + 1 );
	src_copy[ INET6_ADDRSTRLEN ] = 0;

	if ( WSAStringToAddress( src_copy, af, NULL, ( struct sockaddr * )&ss, &size ) == 0 ) {
		switch ( af ) {
		case AF_INET:
			*( struct in_addr * )dst = ( ( struct sockaddr_in * )&ss )->sin_addr;
			return 1;
		case AF_INET6:
			*( struct in6_addr * )dst = ( ( struct sockaddr_in6 * )&ss )->sin6_addr;
			return 1;
		}
	}
	return 0;
}

const char *inet_ntop( int af, const void *src, char *dst, socklen_t size )
{
	struct sockaddr_storage ss;
	unsigned long s = size;

	ZeroMemory( &ss, sizeof( ss ) );
	ss.ss_family = af;

	switch ( af ) {
	case AF_INET:
		( ( struct sockaddr_in * )&ss )->sin_addr = *( struct in_addr * )src;
		break;
	case AF_INET6:
		( ( struct sockaddr_in6 * )&ss )->sin6_addr = *( struct in6_addr * )src;
		break;
	default:
		return NULL;
	}

	return ( WSAAddressToString( ( struct sockaddr * )&ss, sizeof( ss ), NULL, dst, &s ) == 0 ) ? dst : NULL;
}
#endif

#define PACKET_STRICT_VALIDATION
#define PACKET_HEADER_LENGTH		8
#define PACKET_BACKUP_LENGTH		( NET_PAYLOAD_SIZE * 4 )
#define PACKET_TRANSFER_MTU			1500

DWORD WINAPI NET_ProcessSocket( LPVOID lp );

enum channel_state_t
{
	NET_IDLE,
	NET_SENDING,
	NET_RECEIVING
};

class CNetMessageQueue
{
public:
	CNetMessageQueue( INetChannel* pNetChannel );
	~CNetMessageQueue();
	void							AddMessage( INetMessage* pNetMessage );
	void							ProcessMessages();

	void							ReleaseQueue();

	int								GetMessageCount()				const { return m_Queue.size(); }
	INetMessage*					GetMessageByIndex( int nMsg )	const { return m_Queue[ nMsg ]; }
	void							RemoveMessage( int nMsg )		{ m_Queue.erase( m_Queue.begin() + nMsg ); }

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
	INetChannel*					m_pChannel;
};

class CBaseNetChannel : public INetChannel
{
public:
	CBaseNetChannel();
	~CBaseNetChannel();

	bool				Connect( SOCKET hSocket );
	bool				Connect( const char* pszHost, int nPort );
	bool				InitFromSocket( SOCKET hSocket, DWORD dwNetworkThreadId, ServerConnectionNotifyFn pfnNotify );

	void				CloseConnection();

	void				SendNetData( char* pData, long nSize, const bf_write* pProps );
	void				SendNetMessage( INetMessage* pNetMessage );
	void				Transmit( INetMessage* pNetMessage = NULL );
	void				Disconnect( const char* pszReason );
	bool				Reconnect();

	bool				ProcessSocket();
	long				ProcessIncoming();
	long				ProcessOutgoing();

	int					GetTickRate()						const { return m_nTickRate; }
	bool				IsSending()							const { return IsConnected() && ( m_nState == channel_state_t::NET_SENDING ); }
	bool				IsReceiving()						const { return IsConnected() && ( m_nState == channel_state_t::NET_RECEIVING ); }
	bool				IsActiveTransmission()				const { return m_bIsActiveTransmission; }
	bool				IsConnected()						const { return ( m_hSocket != INVALID_SOCKET ); }
	const char*			GetDisconnectReason()				const { return m_szDisconnectReason; }
	const char*			GetHostIPString()					const { return m_szHostIP; }
	unsigned long		GetHostIP()							const { return m_nHostIP; }
	unsigned long		GetFlags()							const { return m_nFlags; }
	long				GetOutgoingSequenceNr()				const { return m_nOutgoingSequenceNr; }
	long				GetIncomingSequenceNr()				const { return m_nIncomingSequenceNr; }
	long				GetTransferSequenceNr()				const { return m_nTransmissionSequenceNr; }
	unsigned int		GetSocket()							const { return m_hSocket; }

	void				SetOutgoingSequenceNr( long nSeq )	{ m_nOutgoingSequenceNr = nSeq; }
	void				SetIncomingSequenceNr( long nSeq )	{ m_nIncomingSequenceNr = nSeq; }
	void				SetTickRate( long nTickRate )		{ m_nTickRate = ( int ) nTickRate; }
	void				SetFlags( unsigned long nFlags )	{ m_nFlags = nFlags; }
	void				SetState( channel_state_t nState )	{ m_nState = nState;}

	bool				IsActiveSocket() const
	{
		if ( m_hNetworkThread == INVALID_HANDLE_VALUE )
			return false;

		DWORD dwExitCode = 0;
		if ( !GetExitCodeThread( m_hNetworkThread, &dwExitCode ) )
			return false;

		if ( dwExitCode != STILL_ACTIVE )
			return false;

		return true;
	}

	void				SetMessageHandler( OnHandlerMessageReceivedFn pfnHandler )
	{
		m_MessageHandler = pfnHandler;
	}

	void				SetTransmissionProxy( OnDataTransmissionProgressFn pfnProxy )
	{
		m_TransmissionProxy = pfnProxy;
	}

	void				SetIntermediateProxy( INetIntermediateContext* pContext )
	{
		m_IntermediateProxy = pContext;
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
	long				m_nState;

	/* Sockets */
	SOCKET				m_hSocket;

	DWORD				m_dwNetworkThreadId;
	HANDLE				m_hNetworkThread;

	CRITICAL_SECTION	m_hResourceLock;

	CNetMessageQueue	m_RecvQueue;
	CNetMessageQueue	m_SendQueue;

	unsigned long		m_nFlags;
	unsigned long		m_nRecvBackupLength;
	char*				m_pRecvBackup;

	char				m_szHostIP[ 32 ];
	unsigned long		m_nHostIP;
	addrinfo*			m_pSockAddr;

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

CNetMessageQueue::CNetMessageQueue( INetChannel* pNetChannel )
{
	InitializeCriticalSection( &m_hQueueLock );
	m_pChannel = pNetChannel;
}

CNetMessageQueue::~CNetMessageQueue()
{
	DeleteCriticalSection( &m_hQueueLock );
}

CBaseNetChannel::CBaseNetChannel() : m_RecvQueue( this ), m_SendQueue( this )
{
	m_bIsServer = false;
	m_bCanReconnect = false;
	m_bHasValidatedProtocol = false;
	m_bIsActiveTransmission = false;
	m_nOutgoingSequenceNr = 0;
	m_nIncomingSequenceNr = 0;
	m_nTransmissionSequenceNr = 0;
	m_dwNetworkThreadId = 0;
	m_nLastPingCycle = 0;
	m_hSocket = INVALID_SOCKET;
	m_pfnNotify = NULL;
	m_MessageHandler = NULL;
	m_TransmissionProxy = NULL;
	m_IntermediateProxy = NULL;
	m_pSockAddr = NULL;
	m_hNetworkThread = INVALID_HANDLE_VALUE;
	m_nTickRate = 32;
	m_nTimeout = 20000;
	m_nState = NET_IDLE;

	m_pRecvBackup = new char[ PACKET_BACKUP_LENGTH ];
	m_nRecvBackupLength = 0;

	m_nHostIP = 0;
	m_szHostIP[ 0 ] = 0;
	m_nFlags = 0;


	strncpy( m_szDisconnectReason, "Connection lost", sizeof( m_szDisconnectReason ) );

	InitializeCriticalSection( &m_hResourceLock );
}

CBaseNetChannel::~CBaseNetChannel()
{
	if( m_pRecvBackup )
		delete[] m_pRecvBackup;

	m_pRecvBackup = NULL;
	DeleteCriticalSection( &m_hResourceLock );

	if ( m_pSockAddr )
	{
		freeaddrinfo( m_pSockAddr );
		m_pSockAddr = NULL;
}
}

bool CBaseNetChannel::Connect( const char* pszHost, int nPort )
{
	if ( IsActiveSocket() )
		return false;

	m_nFlags				= 0;
	m_nLastPingCycle		= 0;
	m_bIsServer				= false;
	m_hNetworkThread		= INVALID_HANDLE_VALUE;

	strncpy( m_szHostIP, pszHost, sizeof( m_szHostIP ) );

	m_nRecvBackupLength = 0;
	memset( m_pRecvBackup, 0, sizeof( char ) * PACKET_BACKUP_LENGTH );

	addrinfo hints;
	ZeroMemory( &hints, sizeof( hints ) );
	hints.ai_family			= AF_INET;
	hints.ai_socktype		= SOCK_STREAM;
	hints.ai_protocol		= IPPROTO_TCP;

	char szPort[ 32 ];
	wsprintfA( szPort, "%i", nPort );

	if ( m_pSockAddr )
	{
		freeaddrinfo( m_pSockAddr );
		m_pSockAddr = NULL;
	}

	if ( getaddrinfo( pszHost, szPort, &hints, &m_pSockAddr ) )
		return false;

	in_addr addr_info;
	inet_pton( AF_INET, pszHost, &addr_info );
	m_nHostIP = addr_info.s_addr;

	m_hSocket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

	BOOL nState = 1;
	setsockopt( m_hSocket, IPPROTO_TCP, TCP_NODELAY, ( char * ) &nState, sizeof( nState ) );
	setsockopt( m_hSocket, SOL_SOCKET, SO_RCVTIMEO, ( char * ) &m_nTimeout, sizeof( m_nTimeout ) );
	setsockopt( m_hSocket, SOL_SOCKET, SO_SNDTIMEO, ( char * ) &m_nTimeout, sizeof( m_nTimeout ) );

	if ( connect( m_hSocket, m_pSockAddr->ai_addr, m_pSockAddr->ai_addrlen ) )
	{
		m_hSocket = INVALID_SOCKET;
		return false;
	}

	m_szLastHostIP = pszHost;
	m_nLastHostPort = nPort;
	m_bCanReconnect = true;

	m_hNetworkThread = CreateThread( NULL, NULL, &NET_ProcessSocket, this, NULL, &m_dwNetworkThreadId );

	CCLCConnect* pClientConnect = new CCLCConnect( this );
	Transmit( pClientConnect );

	return true;
}

bool CBaseNetChannel::Connect( SOCKET hSocket )
{
	if ( IsActiveSocket() )
		return false;

	m_nFlags				= 0;
	m_nLastPingCycle		= 0;
	m_bIsServer				= false;
	m_hNetworkThread		= INVALID_HANDLE_VALUE;
	m_hSocket				= hSocket;

	sockaddr_in addressinfo;
	int nAddrSize = sizeof( addressinfo );

	if ( getpeername( hSocket, ( sockaddr* ) &addressinfo, &nAddrSize ) == SOCKET_ERROR )
		return false;

	inet_ntop( AF_INET, &addressinfo.sin_addr, m_szHostIP, sizeof( m_szHostIP ) );
	m_nHostIP = addressinfo.sin_addr.s_addr;

	m_nRecvBackupLength = 0;
	memset( m_pRecvBackup, 0, sizeof( char ) * PACKET_BACKUP_LENGTH );

	BOOL nState = 1;
	setsockopt( m_hSocket, IPPROTO_TCP, TCP_NODELAY, ( char * ) &nState, sizeof( nState ) );
	setsockopt( m_hSocket, SOL_SOCKET, SO_RCVTIMEO, ( char * ) &m_nTimeout, sizeof( m_nTimeout ) );
	setsockopt( m_hSocket, SOL_SOCKET, SO_SNDTIMEO, ( char * ) &m_nTimeout, sizeof( m_nTimeout ) );

	m_szLastHostIP = m_szHostIP;
	m_nLastHostPort = ntohs( addressinfo.sin_port );
	m_bCanReconnect = true;

	m_hNetworkThread = CreateThread( NULL, NULL, &NET_ProcessSocket, this, NULL, &m_dwNetworkThreadId );

	CCLCConnect* pClientConnect = new CCLCConnect( this );
	Transmit( pClientConnect );

	return true;
}

bool CBaseNetChannel::InitFromSocket( SOCKET hSocket, DWORD dwNetworkThreadId, ServerConnectionNotifyFn pfnNotify )
{
	m_nFlags				= 0;
	m_nLastPingCycle		= 0;
	m_bIsServer				= true;
	m_hNetworkThread		= INVALID_HANDLE_VALUE;
	m_hSocket				= hSocket;

	m_nRecvBackupLength = 0;
	memset( m_pRecvBackup, 0, sizeof( char ) * PACKET_BACKUP_LENGTH );

	BOOL nState = 1;
	setsockopt( m_hSocket, IPPROTO_TCP, TCP_NODELAY, ( char * ) &nState, sizeof( nState ) );
	setsockopt( m_hSocket, SOL_SOCKET, SO_RCVTIMEO, ( char * ) &m_nTimeout, sizeof( m_nTimeout ) );
	setsockopt( m_hSocket, SOL_SOCKET, SO_SNDTIMEO, ( char * ) &m_nTimeout, sizeof( m_nTimeout ) );

	sockaddr_in addressinfo;
	int nAddrSize = sizeof( addressinfo );
	if ( getpeername( hSocket, ( sockaddr* ) &addressinfo, &nAddrSize ) == SOCKET_ERROR )
		return false;

	inet_ntop( AF_INET, &addressinfo.sin_addr, m_szHostIP, sizeof( m_szHostIP ) );
	m_nHostIP = addressinfo.sin_addr.s_addr;

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

	//m_dwNetworkThreadId = dwNetworkThreadId;
	m_hNetworkThread = CreateThread( NULL, NULL, &NET_ProcessSocket, this, NULL, &m_dwNetworkThreadId );

	CSVCConnect* pSVCConnect = new CSVCConnect( this );
	SendNetMessage( pSVCConnect );

	return true;
}

void CBaseNetChannel::CloseConnection()
{
	HANDLE hNetworkThread = INVALID_HANDLE_VALUE;
	DWORD dwNetworkThreadId = 0;
	{
		CRITICAL_SECTION_AUTOLOCK( m_hResourceLock );

	if ( m_hSocket == INVALID_SOCKET )
		return;

		if ( !m_bIsServer )
		{
			if ( !IsActiveSocket() )
				return;
		}

	if ( m_bIsServer && m_pfnNotify )
	{
#ifdef NET_NOTIFY_THREADLOCK
		CRITICAL_SECTION_AUTOLOCK( g_hNotificationLock );
#endif

		m_pfnNotify( this, SV_CLIENTDISCONNECT );
	}

		// moved here
	closesocket( m_hSocket );
	m_hSocket = INVALID_SOCKET;

		hNetworkThread = m_hNetworkThread;
		dwNetworkThreadId = m_dwNetworkThreadId;
	}

	if ( hNetworkThread != INVALID_HANDLE_VALUE && dwNetworkThreadId != GetCurrentThreadId() )
	{
		if ( WaitForSingleObject( hNetworkThread, 4000 ) == WAIT_TIMEOUT )
		{
			OutputDebugStringA( "Terminating thread: waiting timed out!" );
			TerminateThread( hNetworkThread, 0 );
		}
	}


	{
		CRITICAL_SECTION_AUTOLOCK( m_hResourceLock );
		// moved from here (18.8.2018)

	m_SendQueue.ReleaseQueue();
	m_RecvQueue.ReleaseQueue();

	m_nIncomingSequenceNr = 0;
	m_nOutgoingSequenceNr = 0;
		m_bHasValidatedProtocol = false;
	}
}

long CBaseNetChannel::ProcessIncoming()
{
	char* pRecv = NULL;
	if ( RecvInternal( &pRecv, NET_PAYLOAD_SIZE ) == -1 )
	{
		if( pRecv )
		delete[] pRecv;

		return -1;
	}

	if( pRecv )
	delete[] pRecv;

	{
		CRITICAL_SECTION_AUTOLOCK( m_hResourceLock );
	m_RecvQueue.ProcessMessages();
	m_RecvQueue.ReleaseQueue();
	}

	return m_nIncomingSequenceNr;
}

long CBaseNetChannel::ProcessOutgoing()
{
	CRITICAL_SECTION_AUTOLOCK( m_hResourceLock );

	/* Process File Transmissions */
	switch ( ProcessTransmissions() )
	{
	case 0:
		break;
	case 1:
		return -1;
	default:
		return m_nOutgoingSequenceNr;
	}

	m_SendQueue.LockQueue();

		if ( static_cast< int >( 2.0f * ( float ) ( m_nTickRate ) ) < m_nLastPingCycle )
		{
			CNETPing* pNETPing = new CNETPing( this );
		m_SendQueue.AddMessage( pNETPing );
	}

	char* pData = NULL;
	int* nDataLength = NULL;

	bool bTransmissionOK = true;
	int nMsgCount = m_SendQueue.GetMessageCount();

	if ( nMsgCount )
	{
		pData = new char[ NET_PAYLOAD_SIZE * nMsgCount ];
		nDataLength = new int[ nMsgCount ];

	for ( int i = 0; i < nMsgCount; ++i )
	{
		INetMessage* pNetMessage = m_SendQueue.GetMessageByIndex( i );

			char* pMessageData = ( char* ) ( pData + NET_PAYLOAD_SIZE * i );
			nDataLength[ i ] = pNetMessage->Serialize( pMessageData, NET_PAYLOAD_SIZE );

			if ( m_IntermediateProxy )
				m_IntermediateProxy->ProcessOutgoing( ( char* ) ( pMessageData + PACKET_MANIFEST_SIZE ), nDataLength[ i ] - PACKET_MANIFEST_SIZE );
		}
	}

	m_SendQueue.UnLockQueue();

	for ( int i = 0; i < nMsgCount; ++i )
	{
		if ( pData && nDataLength && nDataLength[ i ] > 0 )
		{
			m_nState = NET_SENDING;
			if ( SendInternal( ( char* ) ( pData + NET_PAYLOAD_SIZE * i ), nDataLength[ i ] ) == -1 )
		{
			bTransmissionOK = false;
			break;
		}
		}
	}

	if( nMsgCount )
		m_nLastPingCycle = 0;

	if( pData )
	delete[] pData;

	if( nDataLength )
		delete[] nDataLength;

	m_SendQueue.ReleaseQueue();
	return ( bTransmissionOK ? m_nOutgoingSequenceNr : -1 );
}

long CBaseNetChannel::ProcessTransmissions()
{
	m_SendQueue.LockQueue();

	char* pHeaderData = NULL;
	char* pFileData = NULL;

	long nFileLength = 0;
	long nHeaderLength = 0;

	std::vector< int > RemoveQueue;

	int nMsgCount = m_SendQueue.GetMessageCount();
	for ( int i = 0; i < nMsgCount; ++i )
	{
		INetMessage* pNetMessage = m_SendQueue.GetMessageByIndex( i );

		if ( pNetMessage->GetType() != net_Transfer )
			continue;

		CNETDataTransmission* pHeaderMsg = static_cast< CNETDataTransmission* >( pNetMessage );
		nFileLength = pHeaderMsg->GetTransmissionLength();

		if ( nFileLength <= 0 )
		{
			RemoveQueue.push_back( i );
			continue;
		}

		pHeaderData = new char[ NET_PAYLOAD_SIZE ];
		nHeaderLength = pNetMessage->Serialize( pHeaderData, NET_PAYLOAD_SIZE );

		if ( m_IntermediateProxy )
			m_IntermediateProxy->ProcessOutgoing( ( char* )( pHeaderData + PACKET_MANIFEST_SIZE ), nHeaderLength - PACKET_MANIFEST_SIZE );

		if ( nHeaderLength > 0 )
			pFileData = pHeaderMsg->GetTransmissionData();

		RemoveQueue.push_back( i );
		break;
	}

	m_SendQueue.UnLockQueue();

	long nTransmissionId = 0;
	if ( pHeaderData && nHeaderLength > 0 )
	{
		m_nState = NET_SENDING;

		if ( pFileData && SendInternal( pHeaderData, nHeaderLength ) != -1 )
		{
			/* Start Transfering... */
			nTransmissionId = m_nTransmissionSequenceNr + 1;

			m_bIsActiveTransmission = true;

			int nDataLeft = nFileLength;
			int nTotalBytesSent = 0;

			while ( nDataLeft > 0 )
			{
				int nBytesSent = send( m_hSocket, ( char* ) pFileData + nTotalBytesSent, min( nDataLeft, PACKET_TRANSFER_MTU ), 0 );
				
				if ( nBytesSent == SOCKET_ERROR )
				{
					nTransmissionId = -1;
					break;
				}

				nDataLeft -= nBytesSent;
				nTotalBytesSent += nBytesSent;
}

			m_bIsActiveTransmission = false;
		}
		else
		{
			nTransmissionId = -1;
		}
	}

	m_SendQueue.LockQueue();

	nMsgCount = RemoveQueue.size();
	for ( int i = 0; i < nMsgCount; ++i )
		m_SendQueue.RemoveMessage( RemoveQueue[ i ] );

	m_SendQueue.UnLockQueue();


	if( pHeaderData )
		delete[] pHeaderData;

	if ( pFileData )
		delete[] pFileData;

	return nTransmissionId;
}

bool CBaseNetChannel::ProcessSocket()
{
	m_nState = NET_IDLE;

	if ( !IsConnected() )
		return false;

	++m_nLastPingCycle;

	int nIncomingSequenceNr = m_nIncomingSequenceNr;
	int nSequenceNumber = ProcessIncoming();

	if ( nSequenceNumber != -1 && !m_bIsServer && IsConnected() )
	{
		/* No packets were received this frame */
		nSequenceNumber = ProcessOutgoing();
	}

	if ( nSequenceNumber == -1 )
	{
		m_nFlags |= NET_DISCONNECT_BY_PROTOCOL;
		return false;
	}

	return IsConnected();
}

void CBaseNetChannel::SendNetData( char* pData, long nSize, const bf_write* pProps )
{
	if ( nSize <= 0 )
		return;

	int nTransmissionId = ++m_nTransmissionSequenceNr;

	CNETDataTransmission* pDeltaTransmission = new CNETDataTransmission( this );

	char* pFileBuffer = new char[ nSize ];
	memcpy( pFileBuffer, pData, nSize );

	pDeltaTransmission->SetTransmissionId( nTransmissionId );
	if ( !pDeltaTransmission->Init( pFileBuffer, nSize ) )
	{
		printf( "Unable to init delta transmission: Deleting payload!\n" );
		delete[] pFileBuffer;
		delete pDeltaTransmission;
		return;
}

	if ( pProps )
	{
		pDeltaTransmission->WriteProps( ( char* ) pProps->GetData(), pProps->GetNumBytesWritten() );
	}

	SendNetMessage( pDeltaTransmission );
}

void CBaseNetChannel::SendNetMessage( INetMessage* pNetMessage )
{
	m_SendQueue.AddMessage( pNetMessage );

	if ( m_bIsServer )
	{
		if ( ProcessOutgoing() == -1 )
		{
			m_nFlags |= NET_DISCONNECT_BY_PROTOCOL;
			CloseConnection();
}
	}
}

void CBaseNetChannel::Disconnect( const char* pszReason )
{
	CNETDisconnect* pNETDisconnect = NULL;
	{
		CRITICAL_SECTION_AUTOLOCK( m_hResourceLock );
		if ( pszReason && IsActiveSocket() && m_hSocket != INVALID_SOCKET )
		{
	strncpy( m_szDisconnectReason, pszReason, sizeof( m_szDisconnectReason ) );
			pNETDisconnect = new CNETDisconnect( this, pszReason );
		}
	}

	if( pNETDisconnect )
		Transmit( pNETDisconnect );

	CloseConnection();
}

void CBaseNetChannel::Transmit( INetMessage* pNetMessage )
{
	if ( m_dwNetworkThreadId == GetCurrentThreadId() )
	{
		if( pNetMessage )
			SendNetMessage( pNetMessage );

		if( !m_bIsServer )
			ProcessOutgoing();
	}
	else
	{
		if ( !m_bIsServer )
		{
			long nLastOutgoingSequenceNr = m_nOutgoingSequenceNr;

			if ( pNetMessage )
				SendNetMessage( pNetMessage );

			while ( m_nOutgoingSequenceNr == nLastOutgoingSequenceNr && IsConnected() )
				Sleep( 150 );
		}
		else
		{
			if ( pNetMessage )
				SendNetMessage( pNetMessage );
		}
	}
}

bool CBaseNetChannel::Reconnect()
{
	if ( IsConnected() )
		return true;

	if ( !m_bCanReconnect )
		return false;
	
		if ( !m_bIsServer )
	{
		if ( !IsActiveSocket() )
			return Connect( m_szLastHostIP, m_nLastHostPort );
	}

	return false;
}

void CBaseNetChannel::DisconnectInternal( CNETDisconnect* pNetDisconnect )
{
	m_nFlags |= NET_DISCONNECT_BY_HOST;
	strncpy( m_szDisconnectReason, pNetDisconnect->GetDisconnectReason(), sizeof( m_szDisconnectReason ) );

	if ( m_MessageHandler )
		m_MessageHandler( this, pNetDisconnect );

	CloseConnection();
}

long CBaseNetChannel::SendInternal( void* pBuf, unsigned long nSize )
{
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
	return ++m_nOutgoingSequenceNr;
}

long CBaseNetChannel::RecvInternal( char** pBuf, unsigned long nSize )
{
	/* Check socket state */

	if ( !m_bIsServer )
	{
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
	}

	m_nState = NET_RECEIVING;

	/* Fetch the recv buffer */
	( *pBuf ) = new char[ nSize + m_nRecvBackupLength ];
	memcpy( ( *pBuf ), m_pRecvBackup, m_nRecvBackupLength );

	int nReceived = recv( m_hSocket, ( ( *pBuf ) + m_nRecvBackupLength ), nSize, 0 );

	if ( nReceived <= 0 )
		return -1;

	CRITICAL_SECTION_AUTOLOCK( m_hResourceLock );

	long nPreviousRecvLength = m_nRecvBackupLength;

	memset( m_pRecvBackup, 0, sizeof( char ) * PACKET_BACKUP_LENGTH );
	m_nRecvBackupLength = 0;

	long nBytesSerialized = 0, nPacketsSerialized = 0;
	while ( ( nBytesSerialized - nPreviousRecvLength ) < nReceived )
	{
		char* pData = ( char* ) ( *pBuf ) + nBytesSerialized;

		long nDeltaBytes = ( nReceived - ( nBytesSerialized - nPreviousRecvLength ) );
		if ( nDeltaBytes < ( long )( PACKET_HEADER_LENGTH + PACKET_MANIFEST_SIZE ) )
		{
			memcpy( m_pRecvBackup, pData, nDeltaBytes );
			m_nRecvBackupLength = nDeltaBytes;

			return m_nIncomingSequenceNr;
		}

		int nType = -1;
		long nLength = ProcessPacketHeader( pData, nDeltaBytes, &nType );

		if ( nLength <= 0 )
			return -1;

		if ( nDeltaBytes < nLength + PACKET_HEADER_LENGTH )
		{
			memcpy( m_pRecvBackup, pData, nDeltaBytes );
			m_nRecvBackupLength = nDeltaBytes;

			return --m_nIncomingSequenceNr;
		}

		if ( nLength + PACKET_HEADER_LENGTH > nReceived + nPreviousRecvLength )
			return -1;

		if ( m_bIsServer )
		{
			if ( !m_bHasValidatedProtocol && nType != clc_Connect )
				return -1;
		}

		char* pMessage = ( char* ) pData + PACKET_HEADER_LENGTH + PACKET_MANIFEST_SIZE;

		if ( m_IntermediateProxy )
			m_IntermediateProxy->ProcessIncoming( pMessage, nLength - PACKET_MANIFEST_SIZE );

		/* DeSerialize */
		INetMessage* pNetMessage = NULL;

		switch ( nType )
		{
		case net_Ping:
		{
			pNetMessage = new CNETPing( this );

			if ( !pNetMessage->DeSerialize( pMessage, nLength ) )
			{
				delete pNetMessage;
				return -1;
			}

			break;
		}
		case net_Disconnect:
		{
			pNetMessage = new CNETDisconnect( this );

			if ( !pNetMessage->DeSerialize( pMessage, nLength ) )
			{
				delete pNetMessage;
				return -1;
			}

			break;
		}
		case net_HandlerMsg:
		{
			pNetMessage = new CNETHandlerMessage( this );

			if ( !pNetMessage->DeSerialize( pMessage, nLength ) )
			{
				delete pNetMessage;
				return -1;
			}

			break;
		}
		case net_Transfer:
		{
			CNETDataTransmission* pTransmissionHeader = new CNETDataTransmission( this );

			if ( !pTransmissionHeader->DeSerialize( pMessage, nLength ) )
			{
				delete pTransmissionHeader;
				return -1;
			}

			/*
				Transfer block begin	=> pMessage + pTransmissionHeader->GetHeaderPacketSize() - PACKET_MANIFEST_SIZE;
				Transfer block size		=> min( nDataLeft, nTransmissionDelta )
			*/

			long nMessageOffset					= ( pTransmissionHeader->GetHeaderPacketSize() - PACKET_MANIFEST_SIZE );
			char* pTransmissionData				= ( pMessage + nMessageOffset );

			long nDataLeft						= pTransmissionHeader->GetTransmissionLength();
			long nDataLength					= nDataLeft;

			long nTransmissionDelta				= ( nReceived - ( nBytesSerialized + ( nMessageOffset ) + ( PACKET_MANIFEST_SIZE + PACKET_HEADER_LENGTH ) ) );
			long nAbsTransmissionBlock			= min( nDataLeft, nTransmissionDelta );

			if ( m_TransmissionProxy )
			{
				bf_read& msg_props = pTransmissionHeader->ReadProps();
				m_TransmissionProxy( ( void* ) msg_props.GetData(), msg_props.GetNumBytesLeft(), nAbsTransmissionBlock, nDataLength );
			}

			char* pFileBuffer = new char[ nDataLength ];
			if ( pTransmissionHeader->Init( pFileBuffer, nDataLength ) )
			{
				memcpy( pFileBuffer, pTransmissionData, nAbsTransmissionBlock );

				nDataLeft -= nAbsTransmissionBlock;

				while ( nDataLeft > 0 )
				{
					int nDeltaTransfer = recv( m_hSocket, ( char * ) pFileBuffer + ( nDataLength - nDataLeft ), min( nDataLeft, PACKET_TRANSFER_MTU ), 0 );

					if ( nDeltaTransfer <= 0 )
					{
						delete[] pFileBuffer;
						delete pTransmissionHeader;
				return -1;
					}

					if ( m_TransmissionProxy )
					{
						bf_read& msg_props = pTransmissionHeader->ReadProps();
						m_TransmissionProxy( ( void* ) msg_props.GetData(), msg_props.GetNumBytesLeft(), ( nDataLength - nDataLeft ), nDataLength );
					}

					nDataLeft -= nDeltaTransfer;
				}
			}

			if ( m_MessageHandler )
				m_MessageHandler( this, pTransmissionHeader );

			nBytesSerialized += nAbsTransmissionBlock;

			delete[] pFileBuffer;
			delete pTransmissionHeader;
			break;
		}
		default:
		{
			if ( m_bIsServer )
			{
				switch ( nType )
				{
				case clc_Connect:
				{
					CCLCConnect* pCLCConnect = new CCLCConnect( this );

					if ( !pCLCConnect->DeSerialize( pMessage, nLength ) )
					{
						delete pCLCConnect;
						return -1;
					}

					long nProtoVersion = static_cast< CCLCConnect* >( pCLCConnect )->GetProtocolVersion();
					long nProtoUid = static_cast< CCLCConnect* >( pCLCConnect )->GetProtocolUid();

					m_bHasValidatedProtocol = ( nProtoVersion == ( NET_PROTOCOL_VERSION ^ NET_PROTOCOL_MASK ) ) && ( nProtoUid == NET_PROTOCOL_UID );

					if ( !m_bHasValidatedProtocol )
					{
						delete pCLCConnect;
						return -1;
					}

					delete pCLCConnect;
					break;
				}
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
					{
						delete pNetMessage;
						return -1;
					}

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
	{
		m_Queue[ i ]->ProcessMessage();

		/* We might disconnect while processing a handler message */
		/* Make sure we are still connected to continue processing messages */
		if ( !m_pChannel->IsConnected() )
			break;
	}
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