#pragma once

/* Client messages */
#define clc_Connect			( 1 << 0 )
/* #define clc_Reserved		( 1 << 1 )	*/
/* #define clc_Reserved		( 1 << 2 )	*/
/* #define clc_Reserved		( 1 << 3 )	*/
/* #define clc_Reserved		( 1 << 4 )	*/
/* #define clc_Reserved		( 1 << 5 )	*/
/* #define clc_Reserved		( 1 << 6 )	*/
/* #define clc_Reserved		( 1 << 7 )	*/

/* Server messages */
#define svc_Connect			( 1 << 8 )
/* #define svc_Reserved		( 1 << 9 )	*/
/* #define svc_Reserved		( 1 << 10 )	*/
/* #define svc_Reserved		( 1 << 11 )	*/
/* #define svc_Reserved		( 1 << 12 )	*/
/* #define svc_Reserved		( 1 << 13 )	*/
/* #define svc_Reserved		( 1 << 14 )	*/
/* #define svc_Reserved		( 1 << 15 )	*/

/* Protocol messages */
#define net_Ping			( 1 << 16 )
#define net_Disconnect		( 1 << 17 )
#define net_HandlerMsg		( 1 << 18 )
/* #define net_Reserved		( 1 << 19 )	*/
/* #define net_Reserved		( 1 << 20 )	*/
/* #define net_Reserved		( 1 << 21 )	*/
/* #define net_Reserved		( 1 << 22 )	*/
/* #define net_Reserved		( 1 << 23 )	*/
/* #define net_Reserved		( 1 << 24 )	*/

#define NET_TICKRATE_DEFAULT	32
#define NET_TICKRATE_MAX		128
#define NET_TICKRATE_MIN		2

#define PACKET_MANIFEST_SIZE	( sizeof( long ) )
#define NET_BUFFER_SIZE			4098