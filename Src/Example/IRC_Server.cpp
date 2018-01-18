#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "windows.h"
#include "iostream"
#include "vector"

#include "..\Inc\Channel.h"

#define MAX_CONN_PER_IP			3
#define IRC_DEFAULT_TICKRATE	32
#define IRC_DEFAULT_PORT		"920"

struct chat_client_t
{
public:
	INetChannel*			m_pNetChannel;
	char					m_szUsername[ 128 ];
};

CRITICAL_SECTION				g_hClientLock;
std::vector< chat_client_t >	g_Clients;

void UTIL_ConsoleClearWindow();
void UTIL_ConsoleClearLine();

void NET_MessageHandlerFn( INetChannel* pNetChannel, CNETHandlerMessage* pNetMessage )
{
	CRITICAL_SECTION_AUTOLOCK( g_hClientLock );

	bf_read& stream = pNetMessage->InitReader();
	unsigned char nCommand = stream.ReadByte();

	switch ( nCommand )
	{
	case 0: /* Authentication */
	{
		int nConnectedClient = -1;

		int c = g_Clients.size();
		for ( int i = 0; i < c; ++i )
		{
			if ( g_Clients[ i ].m_pNetChannel->GetSocket() == pNetChannel->GetSocket() )
			{
				nConnectedClient = i;

				stream.ReadString( g_Clients[ i ].m_szUsername, sizeof( g_Clients[ i ].m_szUsername ) );
				printf( "Client '%s' -> '%s'\n", g_Clients[ i ].m_pNetChannel->GetHostIPString(), g_Clients[ i ].m_szUsername );
				break;
			}
		}

		/* Verify availability */
		c = g_Clients.size();
		for ( int i = 0; i < c; ++i )
		{
			if ( i == nConnectedClient )
				continue;

			if ( !strcmp( g_Clients[ i ].m_szUsername, g_Clients[ nConnectedClient ].m_szUsername ) )
			{
				printf( "Rejected client '%s' due to occupied username '%s'\n", pNetChannel->GetHostIPString(), g_Clients[ i ].m_szUsername );
				pNetChannel->Disconnect( "Username unavailable." );
				return;
			}
		}

		/* Inform others */
		if ( nConnectedClient != -1 )
		{
			c = g_Clients.size();
			for ( int i = 0; i < c; ++i )
			{
				//if ( g_Clients[ nConnectedClient ].m_pNetChannel->GetSocket() != pNetChannel->GetSocket() )
				//{
					char szLine[ 1024 ];
					snprintf( szLine, sizeof( szLine ), "%s connected.\n", g_Clients[ nConnectedClient ].m_szUsername );

					CNETHandlerMessage* pChatMessage = new CNETHandlerMessage( g_Clients[ i ].m_pNetChannel );
					bf_write& stream = pChatMessage->InitWriter();

					stream.WriteByte( 0 );
					stream.WriteString( szLine );

					g_Clients[ i ].m_pNetChannel->SendNetMessage( pChatMessage );
				//}
			}
		}

		break;
	}
	case 1: /* Chat Message */
	{
		char szMessage[ 512 ];
		stream.ReadString( szMessage, sizeof( szMessage ) );

		int nClientIndex = -1;

		int c = g_Clients.size();
		for ( int i = 0; i < c; ++i )
		{
			if ( g_Clients[ i ].m_pNetChannel->GetSocket() == pNetChannel->GetSocket() )
			{
				nClientIndex = i;
				break;
			}
		}

		/* Client found */
		if ( nClientIndex != -1 )
		{
			char szLine[ 1024 ];
			snprintf( szLine, sizeof( szLine ), "<%s>: %s\n", g_Clients[ nClientIndex ].m_szUsername, szMessage );

			printf( "%s", szLine );

			/* Echo the message to everyone */

			c = g_Clients.size();
			for ( int i = 0; i < c; ++i )
			{
				/* Since we are using prediction rules, don't echo the message */
				/* to the original sender */
				if ( i == nClientIndex )
					continue;

				CNETHandlerMessage* pChatMessage = new CNETHandlerMessage( g_Clients[ i ].m_pNetChannel );
				bf_write& stream = pChatMessage->InitWriter();

				stream.WriteByte( 0 );
				stream.WriteString( szLine );

				g_Clients[ i ].m_pNetChannel->SendNetMessage( pChatMessage );
			}
		}

		break;
	}
	default:
		break;
	}
}

bool NET_ClientNotifyFn( INetChannel* pNetChannel, int nState )
{
	CRITICAL_SECTION_AUTOLOCK( g_hClientLock );

	switch ( nState )
	{
	case SV_CLIENTCONNECT:
	{
		int nConnectionCount = 0;

		int c = g_Clients.size();
		for ( int i = 0; i < c; ++i )
		{
			if ( g_Clients[ i ].m_pNetChannel->GetHostIP() == pNetChannel->GetHostIP() )
				++nConnectionCount;
		}

		if ( nConnectionCount >= MAX_CONN_PER_IP )
		{
			printf( "Rejected client '%s' due to duplicate connection.\n", pNetChannel->GetHostIPString() );
			pNetChannel->Disconnect( "Duplicate connection." );
		}
		else
		{

			pNetChannel->SetMessageHandler( &NET_MessageHandlerFn );
			printf( "Client '%s' connected.\n", pNetChannel->GetHostIPString() );
		}

		chat_client_t new_client;
		new_client.m_pNetChannel = pNetChannel;
		strncpy( new_client.m_szUsername, "Unknown", sizeof( new_client.m_szUsername ) );

		g_Clients.insert( g_Clients.end(), new_client );
		break;
	}
	case SV_CLIENTDISCONNECT:
	{
		int nClientIndex = -1;
		int c = g_Clients.size();
		for ( int i = c - 1; i >= 0; --i )
		{
			if ( g_Clients[ i ].m_pNetChannel->GetSocket() == pNetChannel->GetSocket() )
			{
				nClientIndex = i;
				break;
			}
		}

		c = g_Clients.size();
		for ( int i = 0; i < c; ++i )
		{
			if ( g_Clients[ i ].m_pNetChannel->GetSocket() != pNetChannel->GetSocket() )
			{
				char szLine[ 1024 ];
				snprintf( szLine, sizeof( szLine ), "%s disconnected (%s)\n", g_Clients[ nClientIndex ].m_szUsername, pNetChannel->GetDisconnectReason() );

				CNETHandlerMessage* pChatMessage = new CNETHandlerMessage( g_Clients[ i ].m_pNetChannel );
				bf_write& stream = pChatMessage->InitWriter();

				stream.WriteByte( 0 );
				stream.WriteString( szLine );

				g_Clients[ i ].m_pNetChannel->SendNetMessage( pChatMessage );
			}
		}

		printf( "Client '%s' disconnected (%s)\n", pNetChannel->GetHostIPString(), pNetChannel->GetDisconnectReason() );

		g_Clients.erase( g_Clients.begin() + nClientIndex );
		break;
	}
	}

	return true;
}

#define CON_COMMAND( name, buffer ) \
if( !strncmp( name, buffer, strlen( name ) ) )

DWORD WINAPI ConsoleThread( LPVOID lp )
{
	char szCommand[ 512 ];

	for ( ;; Sleep( 50 ) )
	{
		scanf( "%s", szCommand );

		CON_COMMAND( "kick", szCommand )
		{
			scanf( " %[^\n]s", szCommand );

			int nClientIndex = -1;
			int nPtr = 128;

			int c = g_Clients.size();
			for ( int i = 0; i < c; ++i )
			{
				char* pszSubStr = strstr( g_Clients[ i ].m_szUsername, szCommand );
				int nCharPtr = ( g_Clients[ i ].m_szUsername - pszSubStr );

				if ( pszSubStr && nCharPtr < nPtr )
				{
					nPtr = nCharPtr;
					nClientIndex = i;
				}
			}

			if ( nClientIndex != -1 )
			{
				g_Clients[ nClientIndex ].m_pNetChannel->Disconnect( "Kicked by administrator" );
			}

			continue;
		}

		CON_COMMAND( "say", szCommand )
		{
			scanf( " %[^\n]s", szCommand );

			char szLine[ 1024 ];
			snprintf( szLine, sizeof( szLine ), "Console: %s\n", szCommand );

			printf( "%s", szLine );

			/* Echo the message to all other clients */

			int c = g_Clients.size();
			for ( int i = 0; i < c; ++i )
			{
				CNETHandlerMessage* pChatMessage = new CNETHandlerMessage( g_Clients[ i ].m_pNetChannel );
				bf_write& stream = pChatMessage->InitWriter();

				stream.WriteByte( 0 );
				stream.WriteString( szLine );

				g_Clients[ i ].m_pNetChannel->SendNetMessage( pChatMessage );
			}

			continue;
		}

		CON_COMMAND( "list", szCommand )
		{
			int c = g_Clients.size();
			for ( int i = 0; i < c; ++i )
			{
				INetChannel* pNetChannel = g_Clients[ i ].m_pNetChannel;
				if ( pNetChannel->IsConnected() )
					printf( "%s -> '%s'\n", pNetChannel->GetHostIPString(), g_Clients[ i ].m_szUsername );
			}

			continue;
		}

		CON_COMMAND( "clear", szCommand )
		{
			UTIL_ConsoleClearWindow();
			continue;
		}

		//CON_COMMAND( "stress", szCommand )
		//{
		//	scanf( " %[^\n]s", szCommand );

		//	int nClientIndex = -1;
		//	int nPtr = 128;

		//	int c = g_Clients.size();
		//	for ( int i = 0; i < c; ++i )
		//	{
		//		char* pszSubStr = strstr( g_Clients[ i ].m_szUsername, szCommand );
		//		int nCharPtr = ( g_Clients[ i ].m_szUsername - pszSubStr );

		//		if ( pszSubStr && nCharPtr < nPtr )
		//		{
		//			nPtr = nCharPtr;
		//			nClientIndex = i;
		//		}
		//	}

		//	if ( nClientIndex != -1 )
		//	{
		//		for ( int i = 0; i < 128; ++i )
		//		{
		//			CNETHandlerMessage* pChatMessage = new CNETHandlerMessage( g_Clients[ nClientIndex ].m_pNetChannel );
		//			bf_write& stream = pChatMessage->InitWriter();

		//			stream.WriteByte( 0 );
		//			stream.WriteString( "Privet\n" );

		//			g_Clients[ nClientIndex ].m_pNetChannel->SendNetMessage( pChatMessage );
		//		}
		//	}

		//	continue;
		//}
	}
}

int main()
{
	printf( "Launching IRC server\n" );
	InitializeCriticalSection( &g_hClientLock );

	if ( !NET_StartUp() )
	{
		DeleteCriticalSection( &g_hClientLock );
		return 0;
	}

	CreateThread( NULL, NULL, &ConsoleThread, NULL, NULL, NULL );

	printf( "Port=%s, Tickrate=%i\n", IRC_DEFAULT_PORT, IRC_DEFAULT_TICKRATE );
	printf( "Awaiting clients...\n" );

	NET_ProcessListenSocket( IRC_DEFAULT_PORT, IRC_DEFAULT_TICKRATE, &NET_ClientNotifyFn );

	NET_Shutdown();

	DeleteCriticalSection( &g_hClientLock );
	return 0;
}

void UTIL_ConsoleClearLine()
{
	COORD cdLine;
	HANDLE hStdOut = GetStdHandle( STD_OUTPUT_HANDLE );
	CONSOLE_SCREEN_BUFFER_INFO sbi;
	DWORD dwWritten;

	GetConsoleScreenBufferInfo( hStdOut, &sbi );

	cdLine.X = 0;
	cdLine.Y = sbi.dwCursorPosition.Y;

	FillConsoleOutputCharacterA( hStdOut, ' ', sbi.dwSize.X, cdLine, &dwWritten );
	FillConsoleOutputAttribute( hStdOut, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE, sbi.dwSize.X, cdLine, &dwWritten );

	SetConsoleCursorPosition( hStdOut, cdLine );
}

void UTIL_ConsoleClearWindow()
{
	COORD cdLine = { 0, 0 };
	HANDLE hStdOut = GetStdHandle( STD_OUTPUT_HANDLE );
	CONSOLE_SCREEN_BUFFER_INFO sbi;
	DWORD dwWritten;

	GetConsoleScreenBufferInfo( hStdOut, &sbi );

	FillConsoleOutputCharacterA( hStdOut, ' ', sbi.dwSize.X * sbi.dwSize.Y, cdLine, &dwWritten );
	FillConsoleOutputAttribute( hStdOut, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE, sbi.dwSize.X * sbi.dwSize.Y, cdLine, &dwWritten );

	SetConsoleCursorPosition( hStdOut, cdLine );
}