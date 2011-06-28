//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.


#define __CONCAT1(x,y)  x ## y
#define __CONCAT(x,y)   __CONCAT1(x,y)

#define UINT64_C(value) __CONCAT(value, ULL) 
#define INT64_C(val) val##i64

extern "C" {

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswscale/swscale.h>
#include <libavcodec/audioconvert.h>
}

#include <vd2/plugin/vdplugin.h>
#include <vd2/plugin/vdinputdriver.h>
#include <vector>
#include <string>
#include <string.h>
#include <math.h>
#include "resource.h"
#include <vd2/VDXFrame/Unknown.h>
#include <vd2/VDXFrame/VideoFilterDialog.h>

#include <list>


#define INPUT_DRIVER_TAG  "[FFMpeg]"

#define FFDRIVER_VERSION_MAJOR		0
#define FFDRIVER_VERSION_MINOR		4
#define FFDRIVER_VERSION_BUILD		101


//Cut-off buffer from demuxer (mostly for not used audio streams);
#define MAX_PACKETS_BUFFER_SIZE 512
#define MAX_PACKETS_DELTA		50
#define MAX_DESYNC_TIME			0.1 //(sec)


class IFFStream;
class IFFSource
{
public:
	virtual AVFormatContext* getContext( void ) = 0;

	virtual bool setStream( IFFStream* pStream ) = 0;

	virtual bool readFrame( IFFStream* pStream ) = 0;
	virtual bool seekFrame(  IFFStream* pStream, int64 timestamp, bool backward = true ) = 0;

};

class IFFStream
{
public:
	//Push read packet to framebuffer; 
	virtual void pushPacket( AVPacket* pPacket ) = 0;
	//Invalidate stack due to seek;
	virtual void invalidateBuffer( void ) = 0;
	virtual void notifySeek( int64 timestamp ) = 0;

	virtual AVPacket* queryPacket( void ) = 0;


	virtual int		getIndex( void ) const = 0;
	virtual sint64 getPts( AVPacket* pPacket ) = 0;



};

class VDFFStreamBase : public IFFStream
{
public:
	typedef std::list<AVPacket>		TPacketList;

public:
	VDFFStreamBase( AVMediaType codecType ):
	  m_eCodecType( codecType ),
		  m_indexStream( -1 ),
		  m_bSequenced( false )
	  {
	  }

	  virtual ~VDFFStreamBase()
	  {
	  }
	  //Init listener and return valid stream of ffmpeg or -1;
	  virtual int	initStream( IFFSource* pSource, int indexStream )
	  {
		  m_pSource = pSource;
		  if ( m_pSource == NULL )
			  return -1;
		  AVFormatContext *pFormatCtx = pSource->getContext();
		  if ( !pFormatCtx ) return -1;

		  for(uint32 i=0; i<pFormatCtx->nb_streams; i++)
		  {
			  if(pFormatCtx->streams[i]->codec->codec_type==m_eCodecType &&
				  indexStream-- <= 0 )
			  {			
				  m_indexStream = i;
				  break;
			  }
		  }

		  if ( m_indexStream < 0 )
			  return -1;
	
		  return m_indexStream;
	  }
	  //Push read packet to framebuffer; 
	  virtual void pushPacket( AVPacket* pPacket )
	  {
		  AVPacket packet = *pPacket;
		  av_dup_packet( &packet );
		  m_packetsBuffer.push_back( packet );

		  while ( m_packetsBuffer.size() > MAX_PACKETS_BUFFER_SIZE )
		  {
			  av_free_packet( &m_packetsBuffer.front() );
			  m_packetsBuffer.pop_front();
		  }
	  }

	  //Invalidate stack due to seek;
	  virtual void invalidateBuffer( void )
	  {
		  while ( !m_packetsBuffer.empty() )
		  {
			  av_free_packet( &m_packetsBuffer.front() );
			  m_packetsBuffer.pop_front();
		  }
		  m_bSequenced = false;
	  }


	  AVPacket* queryPacket( void )
	  {
		  if ( m_packetsBuffer.empty() )
		  {
			  if ( !m_pSource->readFrame( this ) )
			  {
				  return NULL;
			  }
		  }
		  return &m_packetsBuffer.front();
	  }

	  void	popPacket( void )
	  {
		  m_bSequenced = true;
		  if (  !m_packetsBuffer.empty() )
		  {
			  av_free_packet( &m_packetsBuffer.front() );
			  m_packetsBuffer.pop_front();
		  }
	  }

	  bool	seekPacket( int64 timestamp )
	  {
		  if ( m_pSource )
			return m_pSource->seekFrame( this, timestamp );

		  return false;
	  }
	 
	  inline IFFSource*	getSource( void ){ return m_pSource; }
	  inline	int			getIndex( void ) const { return m_indexStream; }

private:
	IFFSource							*m_pSource;
	int									m_indexStream;
	TPacketList							m_packetsBuffer;
	AVMediaType							m_eCodecType;

	bool								m_bSequenced;

};

///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////

class VDFFVideoSource : public vdxunknown<IVDXStreamSource>, public IVDXVideoSource, public IVDXVideoDecoder, public IVDXVideoDecoderModel, public VDFFStreamBase 
{
public:
	VDFFVideoSource(const VDXInputDriverContext& context);
	~VDFFVideoSource();

	int VDXAPIENTRY AddRef();
	int VDXAPIENTRY Release();
	void *VDXAPIENTRY AsInterface(uint32 iid);

public:
	//Stream Interface
	void		VDXAPIENTRY GetStreamSourceInfo(VDXStreamSourceInfo&);
	bool		VDXAPIENTRY Read(sint64 lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead);

	const void *VDXAPIENTRY GetDirectFormat();
	int			VDXAPIENTRY GetDirectFormatLen();

	ErrorMode VDXAPIENTRY GetDecodeErrorMode();
	void VDXAPIENTRY SetDecodeErrorMode(ErrorMode mode);
	bool VDXAPIENTRY IsDecodeErrorModeSupported(ErrorMode mode);

	bool VDXAPIENTRY IsVBR();
	sint64 VDXAPIENTRY TimeToPositionVBR(sint64 us);
	sint64 VDXAPIENTRY PositionToTimeVBR(sint64 samples);

	void VDXAPIENTRY GetVideoSourceInfo(VDXVideoSourceInfo& info);

	bool VDXAPIENTRY CreateVideoDecoderModel(IVDXVideoDecoderModel **ppModel);
	bool VDXAPIENTRY CreateVideoDecoder(IVDXVideoDecoder **ppDecoder);

	void		VDXAPIENTRY GetSampleInfo(sint64 sample_num, VDXVideoFrameInfo& frameInfo);

	bool		VDXAPIENTRY IsKey(sint64 lSample);

	sint64		VDXAPIENTRY GetFrameNumberForSample(sint64 sample_num);
	sint64		VDXAPIENTRY GetSampleNumberForFrame(sint64 display_num);
	sint64		VDXAPIENTRY GetRealFrame(sint64 display_num);

	sint64		VDXAPIENTRY GetSampleBytePosition(sint64 sample_num);

public:
	//Decoder Interface
	const void *VDXAPIENTRY DecodeFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, sint64 streamFrame, sint64 targetFrame);
	uint32		VDXAPIENTRY GetDecodePadding();
	void		VDXAPIENTRY Reset();
	bool		VDXAPIENTRY IsFrameBufferValid();
	const VDXPixmap& VDXAPIENTRY GetFrameBuffer();
	bool		VDXAPIENTRY SetTargetFormat(int format, bool useDIBAlignment);
	bool		VDXAPIENTRY SetDecompressedFormat(const VDXBITMAPINFOHEADER *pbih);

	const void *VDXAPIENTRY GetFrameBufferBase();
	bool		VDXAPIENTRY IsDecodable(sint64 sample_num);

public:
	//Model Interface
	//void	VDXAPIENTRY Reset();
	void	VDXAPIENTRY SetDesiredFrame(sint64 frame_num);
	sint64	VDXAPIENTRY GetNextRequiredSample(bool& is_preroll);
	int		VDXAPIENTRY GetRequiredCount();

public:
	//Internal
	int			initStream( IFFSource* pSource, int indexStream );
	void		invalidateBuffer( void );
	void		notifySeek( int64 timestamp );

	sint64		getPts(  AVPacket* pPacket );

	uint32		prepareFrameBuffer( AVFrame* pFrame, int format, void* pFrameBuffer );
	bool		decodeFramePacket( AVFrame* pFrame, AVPacket* pPacket );
	int64		guessPts( AVPacket* pPacket );

	//Convert frame number to timestamp;
	int64		pos2ts( sint64 num ) const;

	sint64		ts2pos( int64 ts ) const;


private:
	const VDXInputDriverContext&	mContext;
	AVFormatContext					*m_pFormatCtx;
	AVStream						*m_pStreamCtx;
	AVCodecContext					*m_pCodecCtx;

	SwsContext						*m_pSwsCtx;
	VDXStreamSourceInfo				m_streamInfo;

	VDXPixmap						m_pixmap;
	//Buffer for pixmap;
	std::vector<uint8>				m_frameBuffer;
	//Buffers of two sequenced frames;  
	std::vector<uint8>				m_currentBuffer;
	std::vector<uint8>				m_nextBuffer;

	sint64							m_posDecode;
	bool							m_bResetDecoder;
	
private:
	sint64							m_posDesired;
	sint64							m_posCurrent;
	sint64							m_posNext;
	//Threshold for seek;
	sint64							m_posDelta;
	sint64							m_posDesync;
	bool							m_bStreamSeeked;

private:
	int64							m_tsStart;

	
};

VDFFVideoSource::VDFFVideoSource(const VDXInputDriverContext& context):
VDFFStreamBase( AVMEDIA_TYPE_VIDEO ),
	m_posDecode( -1 ),
	m_posDesired(-1),
	m_pFormatCtx( NULL ),
	m_pStreamCtx( NULL ),
	m_pCodecCtx( NULL ),
	m_pSwsCtx( NULL ),
	m_posNext(-1),
	m_tsStart( 0 ),
	m_bStreamSeeked(false),
	mContext(context)
{

}

VDFFVideoSource::~VDFFVideoSource() 
{
	if ( m_pCodecCtx )
		// Close the codec
		avcodec_close(m_pCodecCtx);

	if ( m_pSwsCtx )
		sws_freeContext( m_pSwsCtx );
}


int VDFFVideoSource::AddRef() {
	return vdxunknown<IVDXStreamSource>::AddRef();
}

int VDFFVideoSource::Release() {
	return vdxunknown<IVDXStreamSource>::Release();
}

void *VDXAPIENTRY VDFFVideoSource::AsInterface(uint32 iid)
{
	if (iid == IVDXVideoSource::kIID)
		return static_cast<IVDXVideoSource *>(this);

	return vdxunknown<IVDXStreamSource>::AsInterface(iid);
}



int VDFFVideoSource::initStream( IFFSource* pSource, int streamIndex )
{
	int result = VDFFStreamBase::initStream( pSource, streamIndex );
	if ( result < 0 )
		return result;

	m_pFormatCtx = pSource->getContext();
	m_pStreamCtx = m_pFormatCtx->streams[getIndex()];
	m_pCodecCtx = m_pStreamCtx->codec;

	//TODO:
	//framerate = 1.0/(m_pCodecCtx->ticks_per_frame * m_pCodecCtx->time_base)
	// Find the decoder for the video stream
	AVCodec* pDecoder = avcodec_find_decoder(m_pCodecCtx->codec_id);
	if( pDecoder==NULL ) return -1; // Codec not found

	// Open codec
	if(avcodec_open(m_pCodecCtx, pDecoder)<0)	return -1;

	m_tsStart = 0;

	if ( m_pStreamCtx->duration == AV_NOPTS_VALUE )
		m_streamInfo.mSampleCount = this->ts2pos( m_pFormatCtx->duration * m_pStreamCtx->time_base.den / ( m_pStreamCtx->time_base.num * AV_TIME_BASE ) );
	else
		m_streamInfo.mSampleCount = this->ts2pos( m_pStreamCtx->duration );

	if ( m_pStreamCtx->start_time == AV_NOPTS_VALUE )
		m_tsStart = m_pFormatCtx->start_time * m_pStreamCtx->time_base.den / ( m_pStreamCtx->time_base.num * AV_TIME_BASE );
	else
		m_tsStart = m_pStreamCtx->start_time;

	m_streamInfo.mSampleRate.mNumerator = m_pStreamCtx->r_frame_rate.num;
	m_streamInfo.mSampleRate.mDenominator = m_pStreamCtx->r_frame_rate.den;

	m_streamInfo.mPixelAspectRatio.mNumerator =  1;
	m_streamInfo.mPixelAspectRatio.mDenominator = 1;

	//TODO: check problem with ASPECT in VD
	if ( m_pStreamCtx->sample_aspect_ratio.num )
	{
		m_streamInfo.mPixelAspectRatio.mNumerator =  m_pStreamCtx->sample_aspect_ratio.num;
		m_streamInfo.mPixelAspectRatio.mDenominator = m_pStreamCtx->sample_aspect_ratio.den;
	}
	else if ( m_pCodecCtx->sample_aspect_ratio.num )
	{
		m_streamInfo.mPixelAspectRatio.mNumerator =  m_pCodecCtx->sample_aspect_ratio.num;
		m_streamInfo.mPixelAspectRatio.mDenominator = m_pCodecCtx->sample_aspect_ratio.den;
	}
	

	m_pixmap.w =  m_pCodecCtx->width;
	m_pixmap.h =  m_pCodecCtx->height;

	m_frameBuffer.reserve( m_pCodecCtx->width * m_pCodecCtx->height * 4);
	m_currentBuffer.reserve(  m_pCodecCtx->width * m_pCodecCtx->height * 4 );
	m_nextBuffer.reserve(  m_pCodecCtx->width * m_pCodecCtx->height * 4 );

	m_currentBuffer.resize(1);
	m_nextBuffer.resize(1);

	m_posDelta = MAX_PACKETS_DELTA;
	m_posDesync = ts2pos( (sint64)(MAX_DESYNC_TIME / av_q2d( m_pStreamCtx->time_base )) );

	//Register source;
	if ( !this->getSource()->setStream( this ) )
		return -1;
	m_posCurrent = 0;
	m_posNext = m_posCurrent;

	return result;
}

void VDFFVideoSource::invalidateBuffer( void )
{
	VDFFStreamBase::invalidateBuffer(  );
	//Decode sequence breaked;
	avcodec_flush_buffers( m_pCodecCtx );
	m_posNext = -1;
	m_posCurrent = m_streamInfo.mSampleCount;
	m_bStreamSeeked = true;
}

void VDFFVideoSource::notifySeek( int64 timestamp )
{
	m_posNext = ts2pos( timestamp ) + 1;
	AVPacket *pPacket = queryPacket();
	sint64 pts = getPts( pPacket );
	if ( pPacket && pts != AV_NOPTS_VALUE )
	{
		m_posCurrent = ts2pos( pts );
	}
	else
	{
		m_posCurrent = ts2pos( timestamp ) - 1;
	}
	
}


bool	VDFFVideoSource::decodeFramePacket(  AVFrame* pFrame, AVPacket* pPacket )
{
	int gotFrame = 0;
	AVPacket packet = *pPacket;

	for(;;)
	{
		int len = avcodec_decode_video2( m_pCodecCtx, pFrame, &gotFrame, &packet );
		if ( len >= 0 && len < packet.size )
		{
			packet.data += len;
			packet.size -= len;
		}
		else break;
	}
	return (gotFrame > 0);
}

//Experimental
uint32 VDFFVideoSource::prepareFrameBuffer( AVFrame* pFrame, int format, void* pFrameBuffer )
{
	if ( pFrame ==NULL || pFrame->data[0] == NULL )
		return 0;

	uint8_t *pBuffer = (uint8_t *)pFrameBuffer;//mpFrameBuffer;

	int w = m_pCodecCtx->width;
	int h = m_pCodecCtx->height;

	AVPicture* pPicture = (AVPicture*)pFrame;

	uint8_t* dstData[4] = {NULL, NULL, NULL, NULL};
	int		dstStride[4] = {0, 0, 0, 0};

	uint32 size = 0;

	switch(format) {
	case nsVDXPixmap::kPixFormat_YUV420_Planar:
		{
			dstData[0] = pBuffer;					dstStride[0] = w;
			dstData[1] = dstData[0] + w*h;			dstStride[1] = w >> 1;
			dstData[2] = dstData[1] +  (w*h >> 2);	dstStride[2] = w >> 1;

			size = w*h + (w*h >> 1);
			if ( pBuffer == NULL ) break;

			m_pSwsCtx = sws_getCachedContext(m_pSwsCtx, w, h,
				m_pCodecCtx->pix_fmt,
				w, h, PIX_FMT_YUV420P, SWS_BICUBIC,
				NULL, NULL, NULL);

			sws_scale( m_pSwsCtx, pPicture->data, pPicture->linesize, 0,
				m_pCodecCtx->height, dstData, dstStride);

		}
		break;

	case nsVDXPixmap::kPixFormat_Y8:
		{
			dstData[0] = pBuffer;					dstStride[0] = w;
			dstData[1] = 0;			dstStride[1] = 0;
			dstData[2] = 0;				dstStride[2] = 0;

			size = w*h;
			if ( pBuffer == NULL ) break;

			m_pSwsCtx = sws_getCachedContext(m_pSwsCtx, w, h,
				m_pCodecCtx->pix_fmt,
				w, h, PIX_FMT_YUV420P, SWS_FAST_BILINEAR,
				NULL, NULL, NULL);

			sws_scale( m_pSwsCtx, pPicture->data, pPicture->linesize, 0,
				m_pCodecCtx->height, dstData, dstStride);
		
		}
		break;

	case nsVDXPixmap::kPixFormat_YUV422_UYVY:
		dstData[0] = pBuffer;					dstStride[0] = w*2;

		size = w*2*h;
		if ( pBuffer == NULL ) break;

		m_pSwsCtx = sws_getCachedContext(m_pSwsCtx, w, h,
			m_pCodecCtx->pix_fmt,
			w, h, PIX_FMT_UYVY422, SWS_FAST_BILINEAR,
			NULL, NULL, NULL);

		sws_scale( m_pSwsCtx, pPicture->data, pPicture->linesize, 0,
			m_pCodecCtx->height, dstData, dstStride);

		break;
	case nsVDXPixmap::kPixFormat_YUV422_YUYV:
		dstData[0] = pBuffer;					dstStride[0] = w*2;

		size = w*2*h;
		if ( pBuffer == NULL ) break;

		m_pSwsCtx = sws_getCachedContext(m_pSwsCtx, w, h,
			m_pCodecCtx->pix_fmt,
			w, h, PIX_FMT_YUYV422, SWS_FAST_BILINEAR,
			NULL, NULL, NULL);

		sws_scale( m_pSwsCtx, pPicture->data, pPicture->linesize, 0,
			m_pCodecCtx->height, dstData, dstStride);

		break;
	case nsVDXPixmap::kPixFormat_XRGB1555:
		dstData[0] = pBuffer + w*2*(h-1);					dstStride[0] = -w*2;

		size = w*2*h;
		if ( pBuffer == NULL ) break;

		m_pSwsCtx = sws_getCachedContext(m_pSwsCtx, w, h,
			m_pCodecCtx->pix_fmt,
			w, h, PIX_FMT_BGR555, SWS_BICUBIC,
			NULL, NULL, NULL);

		sws_scale( m_pSwsCtx, pPicture->data, pPicture->linesize, 0,
			m_pCodecCtx->height, dstData, dstStride);
		break;
	case nsVDXPixmap::kPixFormat_RGB565:
		dstData[0] = pBuffer + w*2*(h-1);	dstStride[0] = -w*2;

		size = w*2*h;
		if ( pBuffer == NULL ) break;

		m_pSwsCtx = sws_getCachedContext(m_pSwsCtx, w, h,
			m_pCodecCtx->pix_fmt,
			w, h, PIX_FMT_BGR565, SWS_BICUBIC,
			NULL, NULL, NULL);

		sws_scale( m_pSwsCtx, pPicture->data, pPicture->linesize, 0,
			m_pCodecCtx->height, dstData, dstStride);

		break;
	case nsVDXPixmap::kPixFormat_RGB888:
		dstData[0] = pBuffer + w*3*(h-1); dstStride[0] = -w*3;

		size = w*3*h;
		if ( pBuffer == NULL ) break;

		m_pSwsCtx = sws_getCachedContext(m_pSwsCtx, w, h,
			m_pCodecCtx->pix_fmt,
			w, h, PIX_FMT_BGR24, SWS_BICUBIC,
			NULL, NULL, NULL);

		sws_scale( m_pSwsCtx, pPicture->data, pPicture->linesize, 0,
			m_pCodecCtx->height, dstData, dstStride);

		break;
	case nsVDXPixmap::kPixFormat_XRGB8888:
		dstData[0] = pBuffer + w*4*(h-1); dstStride[0] = -w*4;

		size = w*4*h;
		if ( pBuffer == NULL ) break;

		m_pSwsCtx = sws_getCachedContext(m_pSwsCtx, w, h,
			m_pCodecCtx->pix_fmt,
			w, h, PIX_FMT_BGRA, SWS_BICUBIC,
			NULL, NULL, NULL);

		sws_scale( m_pSwsCtx, pPicture->data, pPicture->linesize, 0,
			m_pCodecCtx->height, dstData, dstStride);

		break;
	}

	return size;
}

void VDXAPIENTRY VDFFVideoSource::GetStreamSourceInfo(VDXStreamSourceInfo& srcInfo)
{
	srcInfo = m_streamInfo;
}

bool VDFFVideoSource::Read(sint64 lStart64, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) 
{
	AVFrame frame, *pFrame = &frame;

	int64 hiTs = this->pos2ts( lStart64 );
		
	if ( (lStart64 > m_posNext + m_posDelta || lStart64 < m_posCurrent) )
	{
		if ( !seekPacket( pos2ts( lStart64 ) ) )
			return false;
	}

	AVPacket* pPacket = NULL;

	bool bGotFrame = false;
	bool bSkipToKey = m_bStreamSeeked;

	while ( lStart64 >= m_posNext || m_bStreamSeeked )
	{
		if ( pPacket ) popPacket();

		pPacket = queryPacket();

		if ( pPacket == NULL ) break;

		//Check if stream seek before key;
	//	if ( bSkipToKey && pPacket->flags != AV_PKT_FLAG_KEY ) continue;
		bSkipToKey = false;

		avcodec_get_frame_defaults( pFrame );

		if ( decodeFramePacket( pFrame, pPacket ) )
		{	
			bGotFrame = true;
			m_posCurrent = m_posNext;

			uint32 size = prepareFrameBuffer( pFrame, m_pixmap.format, NULL );
			if ( !m_bStreamSeeked )
				m_currentBuffer = m_nextBuffer;
			m_nextBuffer.resize( size );
			prepareFrameBuffer( pFrame, m_pixmap.format, &m_nextBuffer[0] );
			
			if ( m_bStreamSeeked )
			{
				m_posCurrent = lStart64;
				m_bStreamSeeked = false;
				m_currentBuffer = m_nextBuffer;
			}
			//BUG: correct next position if no pts;
			sint64 pos = m_posNext;
			if ( pFrame->best_effort_timestamp != AV_NOPTS_VALUE )
				m_posNext = ts2pos( pFrame->best_effort_timestamp );
			else m_posNext += 1;
		}
		
	}

	if ( m_pFormatCtx->pb->eof_reached )
		m_posNext = m_streamInfo.mSampleCount;

	m_bStreamSeeked = false;

	//One byte - marked packet for not copying buffer;
	if ( pPacket ) popPacket();
	
	if (!lpBuffer) {
		if (lSamplesRead) *lSamplesRead = 1;
		if (lBytesRead) *lBytesRead = m_currentBuffer.size();
		return true;
	}

	if ( m_currentBuffer.size() > cbBuffer) {
		if (lSamplesRead) *lSamplesRead = 0;
		if (lBytesRead) *lBytesRead = 0;
		return false;
	}

	if (lSamplesRead) *lSamplesRead = 1;
	if (lBytesRead) *lBytesRead =  m_currentBuffer.size();

	memcpy( lpBuffer, &m_currentBuffer[0], m_currentBuffer.size() );
	
	return true;
}

const void *VDFFVideoSource::GetDirectFormat()
{
	return NULL;
}

int VDFFVideoSource::GetDirectFormatLen() 
{
	return 0;
}

IVDXStreamSource::ErrorMode VDFFVideoSource::GetDecodeErrorMode() 
{
	return IVDXStreamSource::kErrorModeReportAll;
}

void VDFFVideoSource::SetDecodeErrorMode(IVDXStreamSource::ErrorMode mode) 
{
}

bool VDFFVideoSource::IsDecodeErrorModeSupported(IVDXStreamSource::ErrorMode mode)
{
	return mode == IVDXStreamSource::kErrorModeReportAll;
}

bool VDFFVideoSource::IsVBR()
{
	return false;
}

sint64 VDFFVideoSource::TimeToPositionVBR(sint64 us) 
{
	return (sint64)(0.5 + us / 1000000.0 * (double)m_streamInfo.mSampleRate.mNumerator / (double)m_streamInfo.mSampleRate.mDenominator);
}

sint64 VDFFVideoSource::PositionToTimeVBR(sint64 samples) 
{
	return (sint64)(0.5 + samples * 1000000.0 * (double)m_streamInfo.mSampleRate.mDenominator / (double)m_streamInfo.mSampleRate.mNumerator);
}

void VDFFVideoSource::GetVideoSourceInfo(VDXVideoSourceInfo& info)
{
	info.mFlags = 0;
	info.mWidth = m_pCodecCtx->width;
	info.mHeight = m_pCodecCtx->height;
	info.mDecoderModel = VDXVideoSourceInfo::kDecoderModelCustom;
}

bool VDFFVideoSource::CreateVideoDecoderModel(IVDXVideoDecoderModel **ppModel) 
{

	this->AddRef();
	*ppModel = this;
	return true;
}

bool VDFFVideoSource::CreateVideoDecoder(IVDXVideoDecoder **ppDecoder)
{
	this->AddRef();
	*ppDecoder = this;
	return true;
}

void VDFFVideoSource::GetSampleInfo(sint64 sample_num, VDXVideoFrameInfo& frameInfo) 
{
	frameInfo.mBytePosition = -1;

	frameInfo.mFrameType = kVDXVFT_Independent;

	if ( IsKey(sample_num) )
	{
		
		frameInfo.mTypeChar = 'K';
	}
	else
	{
		frameInfo.mTypeChar = 'U';

	}

}

bool VDFFVideoSource::IsKey(sint64 sample)
{
	//EXPERIMENTAL:
	if ( m_pStreamCtx->index_entries )
	{
		int64 timestamp = this->pos2ts( sample );

		int index = av_index_search_timestamp( m_pStreamCtx, timestamp, AVSEEK_FLAG_BACKWARD );

		if ( index >= 0 )
		{	
			sint64 keySample = ts2pos( m_pStreamCtx->index_entries[index].timestamp );
			if ( keySample == sample )
				return true;
			return false;
		}
	}
	return true;
}

sint64 VDFFVideoSource::GetFrameNumberForSample(sint64 sample_num)
{
	return sample_num;
}

sint64 VDFFVideoSource::GetSampleNumberForFrame(sint64 display_num)
{
	return display_num;
}

sint64 VDFFVideoSource::GetRealFrame(sint64 display_num)
{
	return display_num;
}

sint64 VDFFVideoSource::GetSampleBytePosition(sint64 sample_num) {
	return -1;
}

//////////////////////////////////////////////////////////////////////////
//Model
//////////////////////////////////////////////////////////////////////////
void VDFFVideoSource::Reset()
{
	m_posDesired = -1;
	//invalidateBuffer();
}

void VDFFVideoSource::SetDesiredFrame(sint64 frame_num)
{
	m_posDesired = frame_num;
	
}

sint64 VDFFVideoSource::GetNextRequiredSample(bool& is_preroll)
{
	if (m_posCurrent == m_posDesired)
	{
		is_preroll = false;

		return -1;
	}

	sint64 posNext = m_posCurrent + 1;

	is_preroll = false;

	return m_posDesired;
	
}

int VDFFVideoSource::GetRequiredCount() 
{
	return m_posDecode == -1 ? 0 : 1;
}

//////////////////////////////////////////////////////////////////////////
//Decoder
//////////////////////////////////////////////////////////////////////////\

const void *VDFFVideoSource::DecodeFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, sint64 streamFrame, sint64 targetFrame) 
{
	//Check for dummy
	uint8 *pBuffer = &m_frameBuffer[0];

	if ( data_len > 1 )
	{
		memcpy( pBuffer, inputBuffer, data_len );
	}

	m_posDecode = streamFrame;
	
	return &m_frameBuffer[0];
}

uint32 VDFFVideoSource::GetDecodePadding() 
{
	return 0;
}


bool VDFFVideoSource::IsFrameBufferValid()
{
	return m_posDecode >= 0;
}

const VDXPixmap& VDFFVideoSource::GetFrameBuffer()
{
	return m_pixmap;
}

bool VDFFVideoSource::SetTargetFormat(int format, bool useDIBAlignment)
{
	if (format == 0)
		format = nsVDXPixmap::kPixFormat_XRGB8888;

	if (format != nsVDXPixmap::kPixFormat_XRGB8888)
		return false;

	m_frameBuffer.resize(  m_pCodecCtx->width* m_pCodecCtx->height*4 );

	m_pixmap.data			= &m_frameBuffer[0];
	m_pixmap.palette			= NULL;
	m_pixmap.format			= format;
	m_pixmap.w				= m_pCodecCtx->width;
	m_pixmap.h				= m_pCodecCtx->height;
	m_pixmap.pitch			= m_pixmap.w * 4;
	m_pixmap.data2			= NULL;
	m_pixmap.pitch2			= 0;
	m_pixmap.data3			= NULL;
	m_pixmap.pitch3			= 0;

	if (useDIBAlignment)
	{
		m_pixmap.data	= (char *)m_pixmap.data + m_pixmap.pitch*(m_pixmap.h - 1);
		m_pixmap.pitch	= -m_pixmap.pitch;
	}

	Reset();

	return true;
}

bool VDFFVideoSource::SetDecompressedFormat(const VDXBITMAPINFOHEADER *pbih) 
{
	return false;
}

const void *VDFFVideoSource::GetFrameBufferBase()
{
	return &m_frameBuffer[0];
}

bool VDFFVideoSource::IsDecodable(sint64 sample_num64) 
{

	return true;
}

int64 VDFFVideoSource::pos2ts( sint64 num ) const
{
	double scale = m_pStreamCtx->r_frame_rate.den *m_pStreamCtx->time_base.den /(double)( m_pStreamCtx->r_frame_rate.num *m_pStreamCtx->time_base.num );
	return  (int64)(num*scale + 0.5) + m_tsStart;
	
}

sint64 VDFFVideoSource::ts2pos( int64 ts ) const
{
	double scale = m_pStreamCtx->r_frame_rate.num *m_pStreamCtx->time_base.num /(double)( m_pStreamCtx->r_frame_rate.den *m_pStreamCtx->time_base.den );
	return  (sint64)((ts-m_tsStart)*scale + 0.5 );
}

//int64 VDFFVideoSource::guessPts( AVPacket* pPacket )
//{
//	int64_t pts = AV_NOPTS_VALUE;
//
//	if ( pPacket == NULL )
//		return pts;
//
//	if (pPacket->dts != AV_NOPTS_VALUE) 
//	{
//		m_errSum += pPacket->dts <= m_dtsLast;
//		m_dtsLast = pPacket->dts;
//	}
//	if (pPacket->pts != AV_NOPTS_VALUE) 
//	{
//		m_errSum -= pPacket->pts <= m_ptsLast;
//		m_ptsLast = pPacket->pts;
//	}
//	if ((m_errSum >= 0 || pPacket->dts == AV_NOPTS_VALUE)
//		&& pPacket->pts != AV_NOPTS_VALUE)
//		pts = pPacket->pts;
//	else
//		pts = pPacket->dts;
//
//	if ( pts == AV_NOPTS_VALUE )
//		pts = m_ptsGuess;
//	else 
//		m_ptsGuess = pts;
//
//	return pts;
//}

sint64		VDFFVideoSource::getPts( AVPacket* pPacket )
{
	if ( !pPacket ) return 0; 
	sint64 pts = 0;
	if ( pPacket->dts == AV_NOPTS_VALUE	&&
		pPacket->pts != AV_NOPTS_VALUE)
		pts = pPacket->pts;
	else
		pts = pPacket->dts;
	
	return pts;
}

///////////////////////////////////////////////////////////////////////////////
//

#define AUDIO_FRAME_SIZE (AVCODEC_MAX_AUDIO_FRAME_SIZE * 2 )

class VDFFAudioSource : public vdxunknown<IVDXStreamSource>, public IVDXAudioSource, public VDFFStreamBase {
public:
	VDFFAudioSource(const VDXInputDriverContext& context);
	~VDFFAudioSource();

	int VDXAPIENTRY AddRef();
	int VDXAPIENTRY Release();
	void *VDXAPIENTRY AsInterface(uint32 iid);

	void		VDXAPIENTRY GetStreamSourceInfo(VDXStreamSourceInfo&);
	bool		VDXAPIENTRY Read(sint64 lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead);

	const void *VDXAPIENTRY GetDirectFormat();
	int			VDXAPIENTRY GetDirectFormatLen();

	ErrorMode VDXAPIENTRY GetDecodeErrorMode();
	void VDXAPIENTRY SetDecodeErrorMode(ErrorMode mode);
	bool VDXAPIENTRY IsDecodeErrorModeSupported(ErrorMode mode);

	bool VDXAPIENTRY IsVBR();
	sint64 VDXAPIENTRY TimeToPositionVBR(sint64 us);
	sint64 VDXAPIENTRY PositionToTimeVBR(sint64 samples);

	void VDXAPIENTRY GetAudioSourceInfo(VDXAudioSourceInfo& info);

public:
	int initStream( IFFSource* pSource, int indexStream );
	uint32		decodeFramePacket( uint8_t *&pBuffer, AVPacket* pPacket);

	sint64		getPts(AVPacket* pPacket);

	void		invalidateBuffer( void );
	void notifySeek( int64 timestamp );

	uint8*		allocBuffer( uint32 newsize )
	{		
		if ( newsize > m_sizeBuffer )
		{
			uint32 sizeCopy = m_sizeBuffer;
			m_sizeBuffer += FFMAX( AUDIO_FRAME_SIZE, newsize - m_sizeBuffer );
			uint8 *pBuffer = new uint8[m_sizeBuffer];

			if ( m_pBuffer )
			{
				memcpy( pBuffer, m_pBuffer, sizeCopy );
				freeBuffer();
			}

			m_pBuffer = pBuffer;
		}
		return m_pBuffer;
	}

	void	freeBuffer( void )
	{
		if ( m_pBuffer )
			delete [] m_pBuffer;
		m_pBuffer = NULL;
	}

protected:
	VDXWAVEFORMATEX mRawFormat;
	VDXStreamSourceInfo	m_streamInfo;

private:
	uint8_t				*m_pFrameBuffer;
	uint8_t				*m_pAudioBuffer;
	uint8_t				*m_pReformatBuffer;

private:
	AVFormatContext		*m_pFormatCtx;
	AVStream			*m_pStreamCtx;
	AVCodecContext		*m_pCodecCtx;

	SampleFormat		m_fmtSrcAudio;
	AVAudioConvert		*m_pReformatCtx;

	int64				m_posCurrent;
	int64				m_posNext;
	int64				m_posDelta;
	sint64				m_posDesync;
	bool				m_bStreamSeeked;
	int64				m_tsStart;

private:
	//Buffer for decoded samples;
	uint8				*m_pBuffer;
	uint32				m_sizeBuffer;

	const VDXInputDriverContext& mContext;
};

VDFFAudioSource::VDFFAudioSource(const VDXInputDriverContext& context):
VDFFStreamBase( AVMEDIA_TYPE_AUDIO ),
	m_pFormatCtx( NULL ),
	m_pStreamCtx( NULL ),
	m_pCodecCtx( NULL ),
	m_pReformatCtx( NULL ),
	m_bStreamSeeked(true),
	m_pBuffer( NULL ),
	m_pAudioBuffer( NULL ),
	m_pReformatBuffer( NULL ),
	m_pFrameBuffer( NULL ),
	m_sizeBuffer( 0 ),
	mContext(context)
{
	
}

VDFFAudioSource::~VDFFAudioSource()
{
	if (m_pReformatCtx)
		av_audio_convert_free(m_pReformatCtx);

	if ( m_pCodecCtx )
		// Close the codec
		avcodec_close(m_pCodecCtx);

	if ( m_pReformatBuffer )
		av_free( m_pReformatBuffer );

	if ( m_pFrameBuffer )
		av_free( m_pFrameBuffer );

	freeBuffer();
}

int VDFFAudioSource::AddRef()
{
	return vdxunknown<IVDXStreamSource>::AddRef();
}

int VDFFAudioSource::Release()
{
	return vdxunknown<IVDXStreamSource>::Release();
}

void *VDXAPIENTRY VDFFAudioSource::AsInterface(uint32 iid)
{
	if (iid == IVDXAudioSource::kIID)
		return static_cast<IVDXAudioSource *>(this);

	return vdxunknown<IVDXStreamSource>::AsInterface(iid);
}


int	VDFFAudioSource::initStream( IFFSource* pSource, int indexStream )
{
	int result = VDFFStreamBase::initStream( pSource, indexStream );
	if ( result < 0 )
		return result;

	m_pFormatCtx = pSource->getContext();
	m_pStreamCtx = m_pFormatCtx->streams[getIndex()];
	m_pCodecCtx = m_pStreamCtx->codec;

	if (m_pCodecCtx->channels > 0) {
		m_pCodecCtx->request_channels = FFMIN(2, m_pCodecCtx->channels);
	} else {
		m_pCodecCtx->request_channels = 2;
	}
	m_pCodecCtx->request_channel_layout = AV_CH_LAYOUT_STEREO_DOWNMIX;
		
	// Find the decoder for the video stream
	AVCodec* pDecoder = avcodec_find_decoder(m_pCodecCtx->codec_id);
	if( pDecoder==NULL )
		return -1; // Codec not found

	// Open codec
	if(avcodec_open(m_pCodecCtx, pDecoder)<0)
		return -1;

	if ( m_pCodecCtx->request_channels < m_pCodecCtx->channels )
	{
		switch( m_pCodecCtx->channel_layout )
		{
		case AV_CH_LAYOUT_MONO:
		case AV_CH_LAYOUT_STEREO:
		case AV_CH_LAYOUT_5POINT0:
		case AV_CH_LAYOUT_5POINT1:
		case AV_CH_LAYOUT_5POINT0_BACK:
		case AV_CH_LAYOUT_5POINT1_BACK:

			break;

		default:
			mContext.mpCallbacks->SetError("Downmix of %d audio channels is not supported", m_pCodecCtx->channels);
			break;
		}
	}

	if ( !m_pCodecCtx->sample_rate )
		return -1;
	
	m_pFrameBuffer = (uint8*)av_malloc(AUDIO_FRAME_SIZE);
	m_pReformatBuffer = (uint8*)av_malloc(AUDIO_FRAME_SIZE);

	if ( !m_pFrameBuffer || !m_pReformatBuffer )
		return -1;

	if ( m_pStreamCtx->start_time == AV_NOPTS_VALUE )
		m_tsStart = m_pFormatCtx->start_time * m_pStreamCtx->time_base.den / ( m_pStreamCtx->time_base.num * AV_TIME_BASE );
	else
		m_tsStart = m_pStreamCtx->start_time;

	if ( m_pStreamCtx->duration == AV_NOPTS_VALUE )
		m_streamInfo.mSampleCount = m_pFormatCtx->duration * m_pCodecCtx->sample_rate / ( AV_TIME_BASE );
	else
		m_streamInfo.mSampleCount = (sint64)(m_pStreamCtx->duration * av_q2d(m_pStreamCtx->time_base)*m_pCodecCtx->sample_rate);

	m_posDesync = (sint64)(MAX_DESYNC_TIME * m_pCodecCtx->sample_rate);

 	if ( m_pStreamCtx->avg_frame_rate.den )
	{
		m_posDelta = (sint64)(MAX_PACKETS_DELTA /(av_q2d(m_pStreamCtx->avg_frame_rate) * av_q2d(m_pStreamCtx->time_base)) + 0.5);
	}
	else m_posDelta = MAX_PACKETS_DELTA * 1024;


	mRawFormat.mFormatTag		= mRawFormat.kFormatPCM;
	mRawFormat.mChannels		= (WORD) m_pCodecCtx->request_channels;
	mRawFormat.mSamplesPerSec	= m_pCodecCtx->sample_rate;
	mRawFormat.mBitsPerSample	= (WORD)av_get_bits_per_sample_fmt(SAMPLE_FMT_S16);//16
	mRawFormat.mAvgBytesPerSec	= m_pCodecCtx->sample_rate*mRawFormat.mChannels*mRawFormat.mBitsPerSample/8;//pwfex->nSamplesPerSec*4;
	mRawFormat.mBlockAlign		= (WORD)mRawFormat.mChannels*mRawFormat.mBitsPerSample/8;//4;
	
	mRawFormat.mExtraSize			= 0;

	m_streamInfo.mSampleRate.mNumerator = m_pCodecCtx->sample_rate;
	m_streamInfo.mSampleRate.mDenominator = 1;

	//Register source;

	if ( !this->getSource()->setStream( this ) )
		return -1;

	m_posNext = 0;

	return result;

}

void VDFFAudioSource::invalidateBuffer( void )
{
	VDFFStreamBase::invalidateBuffer(  );

	//Decode sequence breaked;
	avcodec_flush_buffers( m_pCodecCtx );

	m_bStreamSeeked = true;
	m_posNext = 0;
	m_posCurrent = m_streamInfo.mSampleCount;
}

void VDFFAudioSource::notifySeek( int64 timestamp )
{
	double scale =  (double)(m_pStreamCtx->time_base.num * m_pCodecCtx->sample_rate)/(double)m_pStreamCtx->time_base.den;

	m_posNext =  (sint64)((timestamp - m_tsStart) * scale) + 1;

	AVPacket * pPacket = queryPacket();

	if ( pPacket )
		m_posCurrent = (sint64)((pPacket->pts - m_tsStart) * scale);
	else
		m_posCurrent = 0;

}


void VDXAPIENTRY VDFFAudioSource::GetStreamSourceInfo(VDXStreamSourceInfo& srcInfo)
{
	srcInfo = m_streamInfo;
}

void downmix2stereo(  sint16* pInput, sint16* pOutput, float left_coeff[6], float right_coeff[6], int channels, int samples )
{
	if( channels > 6 ) return;

	for ( int i = 0; i < samples; i++, pInput+=channels, pOutput+= 2 )
	{
		float left = 0, right = 0;
		for ( int c = 0; c < channels; ++c )
		{
			left += left_coeff[c]*pInput[c];
			right += right_coeff[c]*pInput[c];

		}
		
		pOutput[0] = (sint16)left;
		pOutput[1] = (sint16)right;
	}

}

uint32 VDFFAudioSource::decodeFramePacket( uint8_t  *&pBuffer, AVPacket* pPacket )
{
	int sizeFrame = 0;
	uint32 sizeDecoded = 0;
	AVPacket packet = *pPacket;
	
	 /* NOTE: the audio packet can contain several frames */
    while (packet.size > 0) 
	{
        sizeFrame = AUDIO_FRAME_SIZE;
		
        int len = avcodec_decode_audio3(m_pCodecCtx,
                                    (int16_t *)m_pFrameBuffer, &sizeFrame,
                                    &packet);
        if (len < 0) {
            /* if error, we skip the frame */
            packet.size = 0;
            break;
        }

        packet.data += len;
        packet.size -= len;
        if (sizeFrame <= 0)
            continue;

		/* default channel configurations:
		*
	    * 1ch : front center (mono)
		* 2ch : L + R (stereo)
		* 3ch : front center + L + R
		* 4ch : front center + L + R + back center
		* 5ch : front center + L + R + back stereo
		* 6ch : front center + L + R + back stereo + LFE
		* 7ch : front center + L + R + outer front left + outer front right + back stereo + LFE
		*/

		if (m_pCodecCtx->sample_fmt != m_fmtSrcAudio)
		{
			if (m_pReformatCtx)
				av_audio_convert_free(m_pReformatCtx);
		    m_pReformatCtx= av_audio_convert_alloc(SAMPLE_FMT_S16, 1,
		                                                m_pCodecCtx->sample_fmt, 1, NULL, 0);
		    if (!m_pReformatCtx) 
			{
						mContext.mpCallbacks->SetError("Cannot convert %s sample format to %s sample format\n",
							avcodec_get_sample_fmt_name(m_pCodecCtx->sample_fmt),
							avcodec_get_sample_fmt_name(SAMPLE_FMT_S16));
		                break;
		    }
		    m_fmtSrcAudio = m_pCodecCtx->sample_fmt;
		}

		uint8* pSrcBuffer = m_pFrameBuffer;

		uint32 samplesCount = sizeFrame/ (av_get_bits_per_sample_fmt(m_pCodecCtx->sample_fmt)/8 * m_pCodecCtx->channels );

		if (m_pReformatCtx) 
		{
            
            int istride[6]= {av_get_bits_per_sample_fmt(m_pCodecCtx->sample_fmt)/8};
            int ostride[6]= {2};
            int len = sizeFrame/istride[0];

			/* FIXME: existing code assume that data_size equals framesize*channels*2
			remove this legacy cruft */
			sizeFrame = len * 2;

			const void *ibuf[6]= {m_pFrameBuffer};
			void *obuf[6]= {m_pReformatBuffer};
            if (av_audio_convert(m_pReformatCtx, obuf, ostride, ibuf, istride, len)<0)
			{
				mContext.mpCallbacks->SetError("av_audio_convert() failed\n");
               // VDDEBUG("av_audio_convert() failed\n");
                break;
            }

			pSrcBuffer = m_pReformatBuffer;
			
        }

		uint8* pDstBuffer =  allocBuffer( sizeDecoded + sizeFrame ) + sizeDecoded;

		if ( m_pCodecCtx->channels == m_pCodecCtx->request_channels )
			memcpy( pDstBuffer, m_pFrameBuffer, sizeFrame );
		else
		switch( m_pCodecCtx->channel_layout )
		{
		case AV_CH_LAYOUT_MONO:
		case AV_CH_LAYOUT_STEREO:
			memcpy( pDstBuffer, m_pFrameBuffer, sizeFrame );
			break;

		case AV_CH_LAYOUT_5POINT0:
		case AV_CH_LAYOUT_5POINT1:
			//TODO: update coeff;
		case AV_CH_LAYOUT_5POINT0_BACK:
		case AV_CH_LAYOUT_5POINT1_BACK:
			{
				float cleft[6] = { 0.3225f, 0, 0.2280f, -0.2633f, -0.1862f, 0 };
				float cright[6] = { 0, 0.3225f, 0.2280f, 0.1862f, 0.2633f, 0 };

				downmix2stereo( (sint16*)pSrcBuffer,(sint16*)pDstBuffer, cleft, cright, m_pCodecCtx->channels, samplesCount );
			}
			break;
		
		default:
			mContext.mpCallbacks->SetError("Channel layout not supported!\n");
			break;

		}
		sizeFrame = samplesCount*m_pCodecCtx->request_channels * 2;

		sizeDecoded += sizeFrame;
    }

	pBuffer = m_pBuffer;
	return sizeDecoded/ (2 * m_pCodecCtx->request_channels );

}

bool VDFFAudioSource::Read(sint64 lStart64, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead)
{
	uint32 bytesPerSample = (uint32)m_pCodecCtx->request_channels*av_get_bits_per_sample_fmt(SAMPLE_FMT_S16)/8;
	if ( lpBuffer && cbBuffer < bytesPerSample )
		return false;

	double scale =  m_pStreamCtx->time_base.den / 
		(double)(m_pStreamCtx->time_base.num * m_pCodecCtx->sample_rate);

	uint32 samplesCount = 0;

	if ( (lStart64 > m_posNext + m_posDelta || lStart64 < m_posCurrent) )
	{
		if ( !seekPacket( (sint64)(m_tsStart + lStart64*scale + 0.5)) )
			return false;
	}

	AVPacket* pPacket = NULL;

	while ( lStart64 >= m_posNext || m_bStreamSeeked )
	{
		if ( pPacket ) popPacket();
		pPacket = queryPacket();

		if ( pPacket == NULL ) break;

		int64 sampleNum = (sint64)((pPacket->pts - m_tsStart) / scale);

		if ( samplesCount =  decodeFramePacket( m_pAudioBuffer, pPacket ) )
		{
			//Errors correction
			if ( m_bStreamSeeked || m_posNext <= 0 )
			{
				//Check for sync conditions;
				if ( sampleNum > lStart64)
					m_posNext = lStart64;
				else
					m_posNext = sampleNum;
				
				m_bStreamSeeked = false;
			}

			m_posCurrent = m_posNext;
			m_posNext += samplesCount;

		}

		if ( !samplesCount )
		{
			m_posCurrent = sampleNum;
			m_posNext = sampleNum +(sint64)(pPacket->duration / scale);
		}
	}

	if ( m_posNext > m_streamInfo.mSampleCount )
		m_posNext = m_streamInfo.mSampleCount;

	if ( pPacket ) popPacket();
	m_bStreamSeeked = false;

	if ( m_pFormatCtx->pb->eof_reached )
		m_posNext = m_streamInfo.mSampleCount;

	uint8 *pDst = (uint8*) lpBuffer;
	uint8 *pSrc = m_pAudioBuffer;


	//Copy samples to buffer;
	int32 shift = (int32)(m_posCurrent - lStart64) * bytesPerSample;

	int32 count = 0;

	//Clear previous samples;
	if (lpBuffer && shift > 0 )
	{
		shift = FFMIN( (int32) cbBuffer, shift );
		memset( pDst, 0, shift );
		cbBuffer -= shift;
		pDst += shift;
		count += shift;
		lStart64 = m_posCurrent;
	}

	pSrc -= FFMIN( 0, shift );

	shift = (int32)(m_posNext - lStart64) * bytesPerSample;

	if (lpBuffer && shift > 0 && m_pAudioBuffer)
	{
		shift = FFMIN( shift, (int32)cbBuffer );
		memcpy( pDst, pSrc, shift);
	}

	count = FFMIN( (int32)(lCount * bytesPerSample), count + shift );

	if (lBytesRead)
		*lBytesRead = count;
	if (lSamplesRead)
		*lSamplesRead = count / bytesPerSample;

	return true;
}

const void *VDFFAudioSource::GetDirectFormat() 
{
	return &mRawFormat;
}

int VDFFAudioSource::GetDirectFormatLen() 
{
	return sizeof(mRawFormat);
}

IVDXStreamSource::ErrorMode VDFFAudioSource::GetDecodeErrorMode() 
{
	return IVDXStreamSource::kErrorModeReportAll;
}

void VDFFAudioSource::SetDecodeErrorMode(IVDXStreamSource::ErrorMode mode) 
{
}

bool VDFFAudioSource::IsDecodeErrorModeSupported(IVDXStreamSource::ErrorMode mode)
{
	return mode == IVDXStreamSource::kErrorModeReportAll;
}

bool VDFFAudioSource::IsVBR()
{
	return false;
}

sint64 VDFFAudioSource::TimeToPositionVBR(sint64 us)
{
	return (sint64)(0.5 + us / 1000000.0 * (double)m_streamInfo.mSampleRate.mNumerator / (double)m_streamInfo.mSampleRate.mDenominator);
}

sint64 VDFFAudioSource::PositionToTimeVBR(sint64 samples) 
{
	return (sint64)(0.5 + samples * 1000000.0 * (double)m_streamInfo.mSampleRate.mDenominator / (double)m_streamInfo.mSampleRate.mNumerator);
}

void VDFFAudioSource::GetAudioSourceInfo(VDXAudioSourceInfo& info)
{
	info.mFlags = 0;
}

sint64		VDFFAudioSource::getPts(AVPacket* pPacket)
{
	if ( pPacket )
		return pPacket->pts;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

class VDFFInputFileOptions : public vdxunknown<IVDXInputOptions> {
public:
	bool Read(const void *src, uint32 len);
	uint32 VDXAPIENTRY Write(void *buf, uint32 buflen);

	std::string mArguments;

protected:
	struct Header {
		uint32 signature;
		uint32 size;
		uint16 version;
		uint16 arglen;
	};

	enum { kSignature = VDXMAKEFOURCC('f', 'f', 'm', 'p') };
};

bool VDFFInputFileOptions::Read(const void *src, uint32 len) {
	if (len < sizeof(Header))
		return false;

	Header hdr = {0};
	memcpy(&hdr, src, sizeof(Header));

	if (hdr.signature != kSignature || hdr.version != 1)
		return false;

	if (hdr.size > len || sizeof(Header) + hdr.arglen > len)
		return false;

	const char *args = (const char *)src + sizeof(Header);
	mArguments.assign(args, args + hdr.arglen);
	return true;
}

uint32 VDXAPIENTRY VDFFInputFileOptions::Write(void *buf, uint32 buflen) {
	uint32 required = sizeof(Header) + (uint32)mArguments.size() + 1;
	if (buf) {
		const Header hdr = { kSignature, required, 1, (uint32)mArguments.size() };

		memcpy(buf, &hdr, sizeof(Header));
		memcpy((char *)buf + sizeof(Header), mArguments.data(), mArguments.size());
	}

	return required;
}

///////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
int VDTextWToA(char *dst, int max_dst, const wchar_t *src, int max_src)
{

	*dst = 0;

	int len = WideCharToMultiByte(CP_ACP, 0, src, max_src, dst, max_dst, NULL, NULL);

	// remove null terminator if source was null-terminated (source
	// length was provided)
	return max_src<0 && len>0 ? len-1 : len;
}

void VDDebugCallback(void* ptr, int level, const char* fmt, va_list vl)
{
	static int print_prefix=1;
	static int count = 0;
	static char line[1024], prev[1024];
	static int is_atty;
	AVClass* avc= ptr ? *(AVClass**)ptr : NULL;

	//timeGetSystemTime()
	//if(level>av_log_get_level())
	//	return;
	line[0]=0;

	strcpy( line, INPUT_DRIVER_TAG );
	
	if( avc) 
	{
		sprintf_s(line + strlen(line), sizeof(line) - strlen(line), "[%s @ %p] ", avc->item_name(ptr), ptr);
	}
	


	vsnprintf(line + strlen(line), sizeof(line) - strlen(line), fmt, vl);

	//print_prefix= line[strlen(line)-1] == '\n';


	if(print_prefix && !strcmp(line, prev))
	{
		count++;

		return;
	}
	if(count>0)
	{
		char msg[64];
		sprintf_s(msg, sizeof(msg),"Last message repeated %d times\n", count);
		OutputDebugString(msg);
		count=0;
	}
	//colored_fputs(av_clip(level>>3, 0, 6), line);
	OutputDebugString(line);
	strcpy(prev, line);
}

//////////////////////////////////////////////////////////////////////////

class VDFFInputFile : public vdxunknown<IVDXInputFile>, public IFFSource {
public:
	VDFFInputFile(const VDXInputDriverContext& context);
	~VDFFInputFile();

	void VDXAPIENTRY Init(const wchar_t *szFile, IVDXInputOptions *opts);
	bool VDXAPIENTRY Append(const wchar_t *szFile);

	bool VDXAPIENTRY PromptForOptions(VDXHWND, IVDXInputOptions **);
	bool VDXAPIENTRY CreateOptions(const void *buf, uint32 len, IVDXInputOptions **);
	void VDXAPIENTRY DisplayInfo(VDXHWND hwndParent);

	bool VDXAPIENTRY GetVideoSource(int index, IVDXVideoSource **);
	bool VDXAPIENTRY GetAudioSource(int index, IVDXAudioSource **);

public:
	//virtual bool readFramePacket( IFFStream* pStream, VDPosition pos );
	virtual AVFormatContext* getContext( void ) { return m_pFormatCtx; }

	virtual bool setStream( IFFStream* pStream );

	virtual bool readFrame( IFFStream* pStream );
	virtual bool seekFrame(  IFFStream* pStream, int64 timestamp, bool backward = true );

protected:
	AVFormatContext				*m_pFormatCtx;

	std::vector<IFFStream*>		m_streams;

	const VDXInputDriverContext& mContext;
};

VDFFInputFile::VDFFInputFile(const VDXInputDriverContext& context)
	: mContext(context),
	m_pFormatCtx(NULL)
{
	/* register all codecs, demux and protocols */
	avcodec_register_all();
	// Register all formats and codecs
	av_register_all();
	av_log_set_level( AV_LOG_DEBUG );
	av_log_set_flags(AV_LOG_SKIP_REPEATED);
	av_log_set_callback( VDDebugCallback );
}

VDFFInputFile::~VDFFInputFile()
{
	if ( m_pFormatCtx )
		av_close_input_file(m_pFormatCtx);	

}

void VDFFInputFile::Init(const wchar_t *szFile, IVDXInputOptions *opts) 
{
	char abuf[1024];

	VDTextWToA( abuf, 1024, szFile, -1 );

	//wcstombs(abuf, szFile, 1024);
	abuf[1023] = 0;


	AVFormatParameters params, *ap = &params;

	memset(ap, 0, sizeof(*ap));

	//ap->width = frame_width;
	//ap->height= frame_height;
	ap->time_base.num = 1;
	ap->time_base.den = 25;

	ap->pix_fmt = PIX_FMT_NONE;

	int err = 0;

	// Open video file
	if( (err = av_open_input_file(&m_pFormatCtx, abuf, NULL, 0, ap))!=0)
	{
		mContext.mpCallbacks->SetError("Unable to open file: %ls", szFile);
		return;
	}

	 m_pFormatCtx->flags |= AVFMT_FLAG_GENPTS;

	// Retrieve stream information
	if( (err = av_find_stream_info(m_pFormatCtx))<0)
	{
		mContext.mpCallbacks->SetError("Couldn't find stream information of file: %ls", szFile);
		return;
		
	}
	// Dump information about file onto standard error
	av_dump_format(m_pFormatCtx, 0, abuf, false);

	m_streams.resize( m_pFormatCtx->nb_streams );
}

bool VDFFInputFile::Append(const wchar_t *szFile)
{
	return false;
}

bool VDFFInputFile::setStream( IFFStream* pStream )
{
	if ( pStream == NULL )
	{
		mContext.mpCallbacks->SetError("Used not initialized stream");
		return false;
	}

	m_streams[pStream->getIndex()] = pStream;
	return true;
}

bool VDFFInputFile::readFrame( IFFStream* pStream )
{
	if ( pStream == NULL )
	{
		mContext.mpCallbacks->SetError("Used not initialized stream");
		return false;
	}

	m_streams[pStream->getIndex()] = pStream;

	AVPacket	packet, *pPacket = &packet;
	//Reset packet index;
	av_init_packet( pPacket );
	do
	{
		// Read new packet
		int ret = av_read_frame(m_pFormatCtx, pPacket);

		if (ret < 0) 
		{
			if ((ret == AVERROR_EOF || m_pFormatCtx->pb->eof_reached))
				return false;
			continue;
		}


		if ( m_streams[packet.stream_index] != NULL )
			m_streams[packet.stream_index]->pushPacket( pPacket );
		else 
			av_free_packet(pPacket);

	}
	while ( packet.stream_index != pStream->getIndex());
	
	return true;
}

bool VDFFInputFile::seekFrame( IFFStream* pStream,  int64 timestamp, bool backward )
{
	if ( pStream == NULL )
	{
		mContext.mpCallbacks->SetError("Used not initialized stream");
		return false;
	}

	m_streams[pStream->getIndex()] = pStream;
	double timescale = av_q2d( m_pFormatCtx->streams[pStream->getIndex()]->time_base );

	for ( uint32 i = 0; i < m_streams.size(); ++i )
		if ( m_streams[i] )
			m_streams[i]->invalidateBuffer( );
		

	int ret = av_seek_frame(m_pFormatCtx, pStream->getIndex(), timestamp, /*AVSEEK_FLAG_ANY |*/ backward?AVSEEK_FLAG_BACKWARD:0 );

	if (ret < 0) 
	{
		mContext.mpCallbacks->SetError("Error while seeking file: %s", m_pFormatCtx->filename);
		return false;
	}

	//Correct other streams
	for ( uint32 i = 0; i < m_streams.size(); ++i )
	{
		if ( m_streams[i] )
		{
			sint64 pts = m_streams[i]->getPts(m_streams[i]->queryPacket());
			sint64 reqTs = (sint64) (timestamp * timescale / ( av_q2d( m_pFormatCtx->streams[i]->time_base )) + 0.5 );

			if ( reqTs < pts )
			{
				for ( uint32 j = 0; j < m_streams.size(); ++j )
					if ( m_streams[j] )
						m_streams[j]->invalidateBuffer( );

				ret = av_seek_frame(m_pFormatCtx, i, 2*reqTs - pts, backward?AVSEEK_FLAG_BACKWARD:0 );
				if (ret < 0) 
				{
					mContext.mpCallbacks->SetError("Error while seeking file: %s", m_pFormatCtx->filename);
					return false;
				}
			}
		}
		
	}

	for ( uint32 i = 0; i < m_streams.size(); ++i )
		if ( m_streams[i] )
			m_streams[i]->notifySeek( (int64)(timestamp * timescale /
			(av_q2d( m_pFormatCtx->streams[i]->time_base )) + 0.5 ) );




	return true;

}

//////////////////////////////////////////////////////////////////////////

class VDFFInputFileInfoDialog : public VDXVideoFilterDialog {
public:
	bool Show(VDXHWND parent, VDFFInputFile* pInput);

	virtual INT_PTR DlgProc(UINT msg, WPARAM wParam, LPARAM lParam);

private:
	VDFFInputFile*			m_pInputFile;

};

bool VDFFInputFileInfoDialog::Show(VDXHWND parent, VDFFInputFile* pInput)
{
	m_pInputFile = pInput;
	bool ret = 0 != VDXVideoFilterDialog::Show(NULL, MAKEINTRESOURCE(IDD_FF_INFO), (HWND)parent);

	return ret;
}

INT_PTR VDFFInputFileInfoDialog::DlgProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
	AVFormatContext* pFormatCtx = m_pInputFile->getContext();
	AVInputFormat* pInputFormat = pFormatCtx->iformat;
	switch( msg ) 
	{
	case WM_INITDIALOG:
		{
			char buf[128];

			//HWND hwnd = GetDlgItem(mhdlg, IDC_FORMATNAME);

			SetDlgItemText(mhdlg, IDC_FORMATNAME, pInputFormat->long_name);

			// convert the tick number into the number of seconds
			double seconds =  pFormatCtx->duration/(double)AV_TIME_BASE;
			int hours = (int)(seconds/3600);
			seconds -= (hours * 3600);
			int minutes = (int)(seconds/60);
			seconds -= (minutes * 60);

			//sprintf(buf, "%ld µs", pFormatCtx->duration);
			sprintf(buf, "%d h : %d min : %.2f sec", hours, minutes,seconds);
			SetDlgItemText(mhdlg, IDC_DURATION, buf);

			sprintf(buf, "%.2f sec", pFormatCtx->start_time/(double)AV_TIME_BASE);
			SetDlgItemText(mhdlg, IDC_STARTTIME, buf);

			sprintf(buf, "%u kb/sec", pFormatCtx->bit_rate/1000);
			SetDlgItemText(mhdlg, IDC_BITRATE, buf);

			sprintf(buf, "%u", pFormatCtx->nb_streams);
			SetDlgItemText(mhdlg, IDC_STREAMSCOUNT, buf);

			AVCodecContext *pVideoCtx = NULL;
			AVStream *pVideoStream = NULL;

			for ( int i = 0; i < pFormatCtx->nb_streams; ++i )
			{
				if ( pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO )
				{
					pVideoCtx = pFormatCtx->streams[i]->codec;
					pVideoStream = pFormatCtx->streams[i];
					break;
				}
			}

			//VIDEO Stream

			if ( pVideoStream && pVideoCtx )
			{
				AVCodec* pCodec = avcodec_find_decoder(pVideoCtx->codec_id);
				const char *codec_name = "N/A";
				if (pCodec) 
				{
					codec_name = pCodec->name;

				} else if (pVideoCtx->codec_id == CODEC_ID_MPEG2TS)
				{
					codec_name = "mpeg2ts";
				} else if (pVideoCtx->codec_name[0] != '\0') 
				{
					codec_name = pVideoCtx->codec_name;
				}

				SetDlgItemText(mhdlg, IDC_VIDEO_CODECNAME, codec_name);

				if ( pVideoCtx->pix_fmt != PIX_FMT_NONE )
				{
					SetDlgItemText(mhdlg, IDC_VIDEO_PIXFMT, av_get_pix_fmt_name(pVideoCtx->pix_fmt));
				}
				else
					SetDlgItemText(mhdlg, IDC_VIDEO_PIXFMT,"N/A");

				sprintf(buf, "%u x %u", pVideoCtx->width, pVideoCtx->height);
				SetDlgItemText(mhdlg, IDC_VIDEO_WXH, buf);

				sprintf(buf, "%.2f fps", pVideoStream->r_frame_rate.num/(double)pVideoStream->r_frame_rate.den);
				SetDlgItemText(mhdlg, IDC_VIDEO_FRAMERATE, buf);

				if ( pVideoCtx->bit_rate )
				{
					sprintf(buf, "%u kb/sec", pVideoCtx->bit_rate/1000);
					SetDlgItemText(mhdlg, IDC_VIDEO_BITRATE, buf);

				}
				else
					SetDlgItemText(mhdlg, IDC_VIDEO_BITRATE, "N/A");
				


			}

			AVCodecContext *pAudioCtx = NULL;
			AVStream *pAudioStream = NULL;

			for ( int i = 0; i < pFormatCtx->nb_streams; ++i )
			{
				if ( pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO )
				{
					pAudioCtx = pFormatCtx->streams[i]->codec;
					pAudioStream = pFormatCtx->streams[i];
					break;
				}
			}

			//AUDIO Stream

			if ( pAudioStream && pAudioCtx )
			{
				AVCodec* pCodec = avcodec_find_decoder(pAudioCtx->codec_id);
				const char *codec_name = "N/A";
				if (pCodec) 
				{
					codec_name = pCodec->name;

				} else if (pVideoCtx->codec_name[0] != '\0') 
				{
					codec_name = pAudioCtx->codec_name;
				}

				SetDlgItemText(mhdlg, IDC_AUDIO_CODECNAME, codec_name);

				sprintf(buf, "%u Hz", pAudioCtx->sample_rate);
				SetDlgItemText(mhdlg, IDC_AUDIO_SAMPLERATE, buf);

				sprintf(buf, "%u", pAudioCtx->channels);
				SetDlgItemText(mhdlg, IDC_AUDIO_CHANNELS, buf);

				if (pAudioCtx->sample_fmt != AV_SAMPLE_FMT_NONE) 
				{
					SetDlgItemText(mhdlg, IDC_AUDIO_SAMPLEFMT, av_get_sample_fmt_name(pAudioCtx->sample_fmt));
					
				}
				else 
					SetDlgItemText(mhdlg, IDC_AUDIO_SAMPLEFMT, "N/A");

				av_get_channel_layout_string(buf, 128, pAudioCtx->channels, pAudioCtx->channel_layout);
				SetDlgItemText(mhdlg, IDC_AUDIO_LAYOUT, buf);

			
				int bits_per_sample = av_get_bits_per_sample(pAudioCtx->codec_id);
				int bit_rate = bits_per_sample ? pAudioCtx->sample_rate * pAudioCtx->channels * bits_per_sample : pAudioCtx->bit_rate;
				if ( bit_rate )
				{
					sprintf(buf, "%u kb/sec", bit_rate/1000);
					SetDlgItemText(mhdlg, IDC_AUDIO_BITRATE, buf);

				}
				else
					SetDlgItemText(mhdlg, IDC_AUDIO_BITRATE, "N/A");

			}

		}
		return TRUE;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
			
			case IDOK:
				
				EndDialog(mhdlg, TRUE);
				return TRUE;

			case IDCANCEL:
				EndDialog(mhdlg, FALSE);
				return TRUE;
		}
	}

	return FALSE;
}


//////////////////////////////////////////////////////////////////////////


class VDFFInputFileOptionsDialog : public VDXVideoFilterDialog {
public:
	bool Show(VDXHWND parent, std::string& args);

	virtual INT_PTR DlgProc(UINT msg, WPARAM wParam, LPARAM lParam);

protected:
	std::string mArguments;
};

bool VDFFInputFileOptionsDialog::Show(VDXHWND parent, std::string& args)
{
	bool ret = 0 != VDXVideoFilterDialog::Show(NULL, MAKEINTRESOURCE(IDD_FFMPEG_OPTIONS), (HWND)parent);
	if (ret)
		args = mArguments;

	return ret;
}

INT_PTR VDFFInputFileOptionsDialog::DlgProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_COMMAND) {
		switch(LOWORD(wParam)) {
			case IDOK:
				{
				/*	HWND hwnd = GetDlgItem(mhdlg, IDC_ARGS);

					int len = GetWindowTextLength(hwnd);

					std::vector<char> buf(len + 1);
					char *s = &buf[0];

					len = GetWindowText(hwnd, s, len+1);
					mArguments.assign(s, s+len);*/
				}
				EndDialog(mhdlg, TRUE);
				return TRUE;

			case IDCANCEL:
				EndDialog(mhdlg, FALSE);
				return TRUE;
		}
	}

	return FALSE;
}

bool VDFFInputFile::PromptForOptions(VDXHWND parent, IVDXInputOptions **ppOptions)
{
	VDFFInputFileOptions *opts = new(std::nothrow) VDFFInputFileOptions;
	if (!opts)
		return false;

	opts->AddRef();

	VDFFInputFileOptionsDialog dlg;
	if (dlg.Show(parent, opts->mArguments)) {
		*ppOptions = opts;
		return true;
	} else {
		delete opts;
		*ppOptions = NULL;
		return false;
	}
}

bool VDFFInputFile::CreateOptions(const void *buf, uint32 len, IVDXInputOptions **ppOptions)
{
	VDFFInputFileOptions *opts = new(std::nothrow) VDFFInputFileOptions;
	if (!opts)
		return false;

	if (!opts->Read(buf, len)) {
		mContext.mpCallbacks->SetError("Invalid options structure.");
		delete opts;
		return false;
	}

	*ppOptions = opts;
	opts->AddRef();
	return false;
}

void VDFFInputFile::DisplayInfo(VDXHWND hwndParent) 
{
	VDFFInputFileInfoDialog dlg;
	dlg.Show(hwndParent, this);
}

bool VDFFInputFile::GetVideoSource(int index, IVDXVideoSource **ppVS) 
{

	*ppVS = NULL;

	if (index < 0)
		return false;

	VDFFVideoSource *pVS = new VDFFVideoSource(mContext);

	if (!pVS)
		return false;

	if (pVS->initStream( this, index ) < 0)
	{
		pVS->Release();
		return false;

	}
	*ppVS = pVS;
	pVS->AddRef();
	return true;
}

bool VDFFInputFile::GetAudioSource(int index, IVDXAudioSource **ppAS)
{
	
	*ppAS = NULL;

	if (index < 0)
		return false;

	VDFFAudioSource *pAS = new VDFFAudioSource(mContext);

	if (!pAS)
		return false;

	if (pAS->initStream( this, index ) < 0)
	{
		pAS->Release();
		return false;

	}
	
	*ppAS = pAS;
	pAS->AddRef();
	return true;
}





///////////////////////////////////////////////////////////////////////////////

class VDFFInputFileDriver : public vdxunknown<IVDXInputFileDriver> {
public:
	VDFFInputFileDriver(const VDXInputDriverContext& context);
	~VDFFInputFileDriver();

	int		VDXAPIENTRY DetectBySignature(const void *pHeader, sint32 nHeaderSize, const void *pFooter, sint32 nFooterSize, sint64 nFileSize);
	bool	VDXAPIENTRY CreateInputFile(uint32 flags, IVDXInputFile **ppFile);

protected:
	const VDXInputDriverContext& mContext;
};

VDFFInputFileDriver::VDFFInputFileDriver(const VDXInputDriverContext& context)
	: mContext(context)
{
}

VDFFInputFileDriver::~VDFFInputFileDriver() {
}

int VDXAPIENTRY VDFFInputFileDriver::DetectBySignature(const void *pHeader, sint32 nHeaderSize, const void *pFooter, sint32 nFooterSize, sint64 nFileSize) {
	return -1;
}

bool VDXAPIENTRY VDFFInputFileDriver::CreateInputFile(uint32 flags, IVDXInputFile **ppFile)
{
	IVDXInputFile *p = new VDFFInputFile(mContext);
	if (!p)
		return false;

	*ppFile = p;
	p->AddRef();
	return true;
}

///////////////////////////////////////////////////////////////////////////////

bool VDXAPIENTRY ff_create(const VDXInputDriverContext *pContext, IVDXInputFileDriver **ppDriver)
{
	IVDXInputFileDriver *p = new VDFFInputFileDriver(*pContext);
	if (!p)
		return false;
	*ppDriver = p;
	p->AddRef();
	return true;
}

const uint8 ff_sig[]={
	'F', 255,
	'F', 255,
	'M', 255,
	'P', 255,
};

const VDXInputDriverDefinition ff_input={
	sizeof(VDXInputDriverDefinition),
	VDXInputDriverDefinition::kFlagSupportsVideo,
	0,
	sizeof ff_sig,
	ff_sig,
	L"*.anm|*.asf|*.avi|*.bik|*.dts|*.dxa|*.flv|*.fli|*.flc|*.flx|*.h261"
	L"|*.h263|*.h264|*.m4v|*.mkv|*.mjp|*.mlp|*.mov|*.mp4|*.3gp|*.3g2|*.mj2|*.mvi|*.ts|*.vob"
	L"|*.pmp|*.rm|*.rmvb|*.rpl|*.smk|*.swf|*.vc1|*.wmv",
	L"FFMpeg Supported Files |*.anm;*.asf;*.avi;*.bik;*.dts;*.dxa;"
	L"*.flv;*.fli;*.flc;*.flx;*.h261;*.h263;*.h264;*.m4v;*.mkv;*.mjp;*.mlp;"
	L"*.mov;*.mp4;*.3gp;*.3g2;*.mj2;*.mvi;*.pmp;*.rm;*.rmvb;*.rpl;*.smk;*.swf;*.vc1;*.wmv;*.ts;*.vob",
	L"ffmpeg",
	ff_create
};

extern const VDXPluginInfo ff_plugin={
	sizeof(VDXPluginInfo),
	L"FFMpeg",
	L"Andrey Kovalchuk",
	L"Loads and decode files through ffmpeg libs.",
	(FFDRIVER_VERSION_MAJOR<<24) + (FFDRIVER_VERSION_MINOR<<16) + FFDRIVER_VERSION_BUILD,
	kVDXPluginType_Input,
	0,
	kVDXPlugin_APIVersion,
	kVDXPlugin_APIVersion,
	kVDXPlugin_InputDriverAPIVersion,
	kVDXPlugin_InputDriverAPIVersion,
	&ff_input
};
