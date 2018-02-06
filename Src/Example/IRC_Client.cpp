#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "windows.h"
#include "iostream"

#include "..\Inc\Channel.h"

bool g_bActive;
char g_szClientName[ 128 ];
char g_szHostIP[ 128 ];

void UTIL_ConsoleClearLine();
void UTIL_ConsoleClearWindow();

#define IRC_DEFAULT_PORT 920

void NET_MessageHandlerFn( INetChannel* pNetChannel, CNETHandlerMessage* pNetMessage )
{
	bf_read& stream = pNetMessage->InitReader();

	switch ( stream.ReadByte() )
	{
	case 0: /* Chat message */
	{
		char szFullLine[ 1024 ];
		stream.ReadString( szFullLine, sizeof( szFullLine ) );

		UTIL_ConsoleClearLine();

		printf( "%s", szFullLine );
		printf( "<%s>: ", g_szClientName );
		break;
	}
	default:
		break;
	}
}

DWORD WINAPI ClientConsoleFn( LPVOID lp )
{
	INetChannel* pNetChannel = ( INetChannel* ) lp;

	char szCommand[ 512 ];
	for ( ;; Sleep( 20 ) )
	{
		if ( pNetChannel->IsConnected() )
		{
			UTIL_ConsoleClearLine();
			printf( "<%s>: ", g_szClientName );
			scanf( " %[^\n]s", szCommand );

			if ( !g_bActive )
				break;

			if ( pNetChannel->IsConnected() )
			{
				if ( !strncmp( szCommand, "disconnect", sizeof( szCommand ) ) )
				{
					pNetChannel->Disconnect( "Disconnect by user." );
					break;
				}
				else
				{
					CNETHandlerMessage* pNetMessage = new CNETHandlerMessage( pNetChannel );
					bf_write& stream = pNetMessage->InitWriter();

					stream.WriteByte( 1 );
					stream.WriteString( szCommand );

					pNetChannel->SendNetMessage( pNetMessage );
				}
			}
		}
	}

	return 0;
}

int main()
{
	if ( !NET_StartUp() )
		return 0;

	g_bActive = true;

	printf( "Host: " );
	scanf( "%[^\n]s", g_szHostIP );

	if ( strlen( g_szHostIP ) == 0 )
		strcpy( g_szHostIP, "127.0.0.1" );

	printf( "Username: " );
	scanf( " %[^\n]s", g_szClientName );

	printf( "Connecting..." );
	INetChannel* pNetChannel = NET_CreateChannel();
	pNetChannel->SetMessageHandler( &NET_MessageHandlerFn );

	while ( !pNetChannel->Connect( g_szHostIP, IRC_DEFAULT_PORT ) )
		Sleep( 1000 );

	printf( "Ok\n" );

	CNETHandlerMessage* pClientHello = new CNETHandlerMessage( pNetChannel );
	bf_write& stream = pClientHello->InitWriter();

	stream.WriteByte( 0 );
	stream.WriteString( g_szClientName );

	pNetChannel->SendNetMessage( pClientHello );
	pNetChannel->Transmit();

	HANDLE hConsoleThread = CreateThread( NULL, NULL, &ClientConsoleFn, pNetChannel, NULL, NULL );

	int nConnectionCount = 0;
	for ( ;; Sleep( 500 ) )
	{
		if ( !pNetChannel->IsConnected() )
		{
			if ( !( pNetChannel->GetFlags() & NET_DISCONNECT_BY_HOST ) )
			{
				++nConnectionCount;
				if ( !pNetChannel->Reconnect() )
				{
					UTIL_ConsoleClearWindow();
					printf( "Reconnecting... %i\n", nConnectionCount );
					Sleep( 1000 );
				}
				else
				{
					printf( "Reconnected!\n" );
				}
			}
			else
			{
				if ( nConnectionCount == 0 )
				{
					UTIL_ConsoleClearWindow();
					printf( "Disconnected: %s\n", pNetChannel->GetDisconnectReason() );
					nConnectionCount = 1;
				}
			}
		}
		else
		{
			if ( nConnectionCount != 0 )
			{
				CNETHandlerMessage* pClientHello = new CNETHandlerMessage( pNetChannel );
				bf_write& stream = pClientHello->InitWriter();

				stream.WriteByte( 0 );
				stream.WriteString( g_szClientName );

				pNetChannel->SendNetMessage( pClientHello );
				pNetChannel->Transmit();

				UTIL_ConsoleClearWindow();
				printf( "<%s>: ", g_szClientName );
			}

			nConnectionCount = 0;
		}
	}

	g_bActive = false;

	if ( WaitForSingleObject( hConsoleThread, 1000 ) == WAIT_TIMEOUT )
		TerminateThread( hConsoleThread, 0 );

	NET_DestroyChannel( pNetChannel );
	NET_Shutdown();

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
	COORD cdLine ={ 0, 0 };
	HANDLE hStdOut = GetStdHandle( STD_OUTPUT_HANDLE );
	CONSOLE_SCREEN_BUFFER_INFO sbi;
	DWORD dwWritten;

	GetConsoleScreenBufferInfo( hStdOut, &sbi );

	FillConsoleOutputCharacterA( hStdOut, ' ', sbi.dwSize.X * sbi.dwSize.Y, cdLine, &dwWritten );
	FillConsoleOutputAttribute( hStdOut, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE, sbi.dwSize.X * sbi.dwSize.Y, cdLine, &dwWritten );

	SetConsoleCursorPosition( hStdOut, cdLine );
}