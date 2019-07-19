#pragma once

class bf_write
{
public:

	void				Init( void *pData, int nBytes )				{ m_pData = (unsigned char *) pData; m_nDataBytes = nBytes; m_nCurByte = 0; }
	void				Reset()										{ m_nCurByte = 0; }
	void				WriteChar( char val )						{ *(char *) ( m_pData + m_nCurByte ) = val; m_nCurByte += 1; }
	void				WriteByte( unsigned char val )				{ *(unsigned char *) ( m_pData + m_nCurByte ) = val; m_nCurByte += 1; }
	void				WriteShort( short val )						{ *(short *) ( m_pData + m_nCurByte ) = val; m_nCurByte += 2; }
	void				WriteWord( unsigned short val )				{ *(unsigned short *) ( m_pData + m_nCurByte ) = val; m_nCurByte += 2; }
	void				WriteLong( long val )						{ *(long *) ( m_pData + m_nCurByte ) = val; m_nCurByte += 4; }
	void				WriteDWord( unsigned long val )				{ *(unsigned long *) ( m_pData + m_nCurByte ) = val; m_nCurByte += 4; }
	void				WriteFloat( float val )						{ *(float *) ( m_pData + m_nCurByte ) = val; m_nCurByte += 4; }
	void				WriteBytes( const void *pBuf, int nBytes );
	void				WriteString( const char *pStr );

	unsigned char		*GetData() const							{ return m_pData; }
	unsigned long		GetNumBytesLeft() const						{ return m_nDataBytes - m_nCurByte; }
	unsigned long		GetNumBytesWritten() const					{ return m_nCurByte; }

protected:
	unsigned char		*m_pData;
	unsigned long		m_nDataBytes;
	unsigned long		m_nCurByte;
};

__forceinline void bf_write::WriteBytes( const void *pBuf, int nBytes )
{
	int nStart = 0;

	do
	{
		WriteByte( *(unsigned char *) ( (unsigned char *) pBuf + nStart ) );
		++nStart;
		--nBytes;
	}
	while ( nBytes );
}

__forceinline void bf_write::WriteString( const char *pStr )
{
	int nStart = 0;

	if ( pStr )
	{
		do
		{
			WriteChar( *pStr );
			++nStart;
			++pStr;
		}
		while ( *( pStr - 1 ) != 0 );
	}
	else
	{
		WriteChar( 0 );
	}
}

class bf_read
{
public:

	void				Init( void *pData, int nBytes )				{ m_pData = (unsigned char *) pData; m_nDataBytes = nBytes; m_nCurByte = 0; }
	void				Reset()										{ m_nCurByte = 0; }
	char				ReadChar()									{ char ret = *(char *) ( m_pData + m_nCurByte ); m_nCurByte += 1; return ret; }
	unsigned char		ReadByte()									{ unsigned char ret = *(unsigned char *) ( m_pData + m_nCurByte ); m_nCurByte += 1; return ret; }
	short				ReadShort()									{ short ret = *(short *) ( m_pData + m_nCurByte ); m_nCurByte += 2; return ret; }
	unsigned short		ReadWord()									{ unsigned short ret = *(unsigned short *) ( m_pData + m_nCurByte ); m_nCurByte += 2; return ret; }
	long				ReadLong()									{ long ret = *(long *) ( m_pData + m_nCurByte ); m_nCurByte += 4; return ret; }
	unsigned long		ReadDWord()									{ unsigned long ret = *(unsigned long *) ( m_pData + m_nCurByte ); m_nCurByte += 4; return ret; }
	float				ReadFloat()									{ float ret = *(float *) ( m_pData + m_nCurByte ); m_nCurByte += 4; return ret; }
	void				ReadBytes( void *pOut, int nBytes );
	int					ReadString( char *pStr, int bufLen );

	const unsigned char	*GetData() const							{ return m_pData; }
	unsigned long		GetNumBytesLeft() const						{ return m_nDataBytes - m_nCurByte; }
	unsigned long		GetNumBytesRead() const						{ return m_nCurByte; }

public:
	const unsigned char	*m_pData;
	unsigned long		m_nDataBytes;
	unsigned long		m_nCurByte;
};

__forceinline void bf_read::ReadBytes( void *pOut, int nBytes )
{
	int nStart = 0;

	do
	{
		*(unsigned char *) ( (int) pOut + nStart ) = ReadByte();
		++nStart;
		--nBytes;
	}
	while ( nBytes );
}

__forceinline int bf_read::ReadString( char *pStr, int bufLen )
{
	int nStart = 0;
	char c = 0;

	do
	{
		c = ReadChar();
		*(char *) ( pStr + nStart ) = c;
		++nStart;
	}
	while ( nStart < bufLen && c );

	return nStart - 1;
}
