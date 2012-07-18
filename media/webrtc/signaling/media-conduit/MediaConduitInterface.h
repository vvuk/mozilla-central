/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MEDIA_CONDUIT_ABSTRACTION_
#define MEDIA_CONDUIT_ABSTRACTION_

#include "nspr.h"
#include "prerror.h"

#include "nsISupportsImpl.h"
#include "nsXPCOM.h"
#include "mozilla/RefPtr.h"
#include "CodecConfig.h"
#include "VideoTypes.h"
#include "MediaConduitErrors.h"


//TODO:crypt:Implemement the interfaces as nsCOMPtr for Ref-Counting 

namespace mozilla {
/** 
 * Abstract Interface for transporting RTP packets - audio/vidoeo
 * The consumers of this interface are responsible for passing in
 * the RTPfied media packets 
 */
class TransportInterface 
{
public:
  virtual ~TransportInterface() {};

  /**
   * RTP Transport Function to be implemented by concrete transport implementation
   * @param data : RTP Packet (audio/video) to be transported
   * @param len  : Length of the media packet
   * @result     : NS_OK on success, NS_ERROR_FAILURE otherwise
   */
  virtual nsresult SendRtpPacket(const void* data, int len) = 0;

  /**
   * RTCP Transport Function to be implemented by concrete transport implementation
   * @param data : RTCP Packet to be transported
   * @param len  : Length of the RTCP packet
   * @result     : NS_OK on success, NS_ERROR_FAILURE otherwise
   */
  virtual nsresult SendRtcpPacket(const void* data, int len) = 0;
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(TransportInterface)
};


/**
 * 1. Abstract renderer for video data
 * 2. This class acts as abstract interface between the video-engine and 
 *    video-engine agnostic renderer implementation.
 * 3. Concrete implementation of this interface is responsible for 
 *    processing and/or rendering the obtained raw video frame to appropriate
 *    output , say, <video>
 */ 
class VideoRenderer 
{
 public:
  virtual ~VideoRenderer() {} ; 

  /**
   * Callback Function reportng any change in the video-frame dimensions
   * @param width:  current width of the video @ decoder
   * @param height: current height of the video @ decoder
   * @param number_of_streams: number of participating video streams
   */
  virtual void FrameSizeChange(unsigned int width,
                               unsigned int height,
                               unsigned int number_of_streams) = 0;

  /**
   * Callback Function reporting decoded frame for processing.
   * @param buffer: pointer to decoded video frame
   * @param buffer_size: size of the decoded frame
   * @param time_stamp: Decoder timestamp, typically 90KHz as per RTP
   * @render_time: Wall-clock time at the decoder for synchronizartion 
   *                purposes
   * NOTE: It is the responsibility of the concrete implementation of this
   * class to own copy of the frame if needed for time longer than scope of
   * this callback. 
   * Such implementations should be quick in processing the frames and return
   * immediately.
   */
  virtual void RenderVideoFrame(const unsigned char* buffer,
                                unsigned int buffer_size,
                                uint32_t time_stamp,
                                int64_t render_time) = 0;

   NS_INLINE_DECL_THREADSAFE_REFCOUNTING(VideoRenderer)

};



 /**
  * 1. Abstract renderer for audio data
  * 2. This class acts as abstract interface between the audio-engine and 
  *    audio-engine agnostic renderer implementations.
  * 3. Concrete implementations of this interface is responsible for 
  *    processing and/or rendering the obtained raw audio samples to
  *    appropriate outputs say, <audio> 
  */

class AudioRenderer 
{
public: 

  virtual ~AudioRenderer() {}; 

  // This method is called to render a audio sample
  /**
   * Callback function reporting decoded audio-samples for processing
   * @param speechData: pointer to audio data of lengthSample
   * @param samplingFreqHz: Sampling rate. Supported rates include
   *                        8000, 16000, 32000, 44000, 48000
   * @param lengthSample: Output parameter reporting length of the samples
   * Note: It is responsibility of the concrete implementation to manange
   * memory for the speechData
   */
  virtual void RenderAudioFrame(int16_t speechData[],
                                uint32_t samplingFreqHz,
                                int lengthSample) = 0;

   NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AudioRenderer)

};

/**
 * Generic Interface for representing Audio/Video Session 
 * MediaSession conduit is identified by 2 main components
 * 1. Attached Transport Interface for inbound and outboud RTP transport
 * 2. Attached Renderer Interface for rendering media data off the network
 * This class hides specifics of Media-Engine implementation from the consumers
 * of this interface.
 * Also provides API configuring the media sent and recevied 
 */
class MediaSessionConduit 
{
public:
  virtual ~MediaSessionConduit() {};

  /**
   * Function triggered on Incoming RTP packet from the remote
   * endpoint by the transport implementation.
   * @param data : RTP Packet (audio/video) to be processed 
   * @param len  : Length of the media packet
   * Obtained packets are passed to the Media-Engine for further
   * processing , say, decoding 
   */
  virtual MediaConduitErrorCode ReceivedRTPPacket(const void *data, int len) = 0;

  /**
   * Function triggered on Incoming RTCP packet from the remote
   * endpoint by the transport implementation.
   * @param data : RTCP Packet (audio/video) to be processed 
   * @param len  : Length of the media packet
   * Obtained packets are passed to the Media-Engine for further
   * processing , say, decoding 
   */
  virtual MediaConduitErrorCode ReceivedRTCPPacket(const void *data, int len) = 0;  


  /**
   * Function to attach Transport end-point of the Media conduit.
   * @param aTransport: Reference to the concrete teansport implementation 
   * NOTE: Owner ship of the aTransport is not maintained by the conduit
   */
  virtual MediaConduitErrorCode AttachTransport(
                                      RefPtr<TransportInterface> aTransport) = 0;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaSessionConduit)

};


/**
 * MediaSessionConduit for video
 * Refer to the comments on MediaSessionConduit above for overall
 * information
 */
class VideoSessionConduit : public MediaSessionConduit
{
public:
  /**
   * Factory function to create and initialize a Video Conduit Session
   * return: Concrete VideoSessionConduitObject or NULL in the case 
   *         of failure 
   */
  static RefPtr<VideoSessionConduit> Create();

  virtual ~VideoSessionConduit() {};

  /**
   * Function to attach Renderer end-point of the Media-Video conduit.
   * @param aRenderer : Reference to the concrete Video renderer implementation 
   */
  virtual MediaConduitErrorCode AttachRenderer(RefPtr<VideoRenderer> aRenderer) = 0;

  /**
   * Function to deliver a capture video frame for encoding and transport
   * @param video_frame: pointer to captured video-frame. 
   * @param video_frame_length: size of the frame
   * @param width, height: dimensions of the frame
   * @param video_type: Type of the video frame - I420, RAW
   * @param captured_time: timestamp when the frame was captured. 
   *                       if 0 timestamp is automatcally generated
   * NOTE: Calling function shouldn't release the frame till this function
   * completes to the fullest. 
   */
  virtual MediaConduitErrorCode SendVideoFrame(unsigned char* video_frame,
                                               unsigned int video_frame_length,
                                                unsigned short width,
                                                unsigned short height,
                                                VideoType video_type,
                                                uint64_t capture_time) = 0;

  /**
   * Function to configure send codec for the video session
   * @param sendSessionConfig: CodecConfiguration
   */
  virtual MediaConduitErrorCode ConfigureSendMediaCodec(const VideoCodecConfig* sendSessionConfig) = 0;

  /**
   * Function to configure receive codec for the video session
   * @param sendSessionConfig: CodecConfiguration
   */
  virtual MediaConduitErrorCode ConfigureRecvMediaCodec(const VideoCodecConfig* recvSessionConfig) = 0;

};

/**
 * MediaSessionConduit for audio 
 * Refer to the comments on MediaSessionConduit above for overall
 * information
 */
class AudioSessionConduit : public MediaSessionConduit 
{
public:

   /**
    * Factory function to create and initialize a Video Conduit Session
    * return: Concrete VideoSessionConduitObject or NULL in the case 
    *         of failure 
    */
  static mozilla::RefPtr<AudioSessionConduit> Create();

  virtual ~AudioSessionConduit() {}; 

  /**
   * Function to attach Renderer end-point of the Media-Video conduit.
   * @param aRenderer : Reference to the concrete Video renderer
   * implementation 
   * NOTE: Owner ship of the aRenderer is not maintained by the conduit
   */
  virtual MediaConduitErrorCode AttachRenderer(
                                       RefPtr<AudioRenderer> aRenderer) = 0;

  /**
   * Function to deliver externally captured audio samples for encoding and transport
   * @param speechData: Audio samples buffer of length samples
   * @param lengthSamples: Number of 16 bit samples in the buffer 
   * @param samplingFreqHz: Frequency/rate of the sampling 
   * @param capture_time: Sample captured timestamp in milliseconds
   */
  virtual MediaConduitErrorCode SendAudioFrame(const int16_t speechData[], 
                                                unsigned int lengthSamples,
                                                uint32_t samplingFreqHz,
                                                uint64_t capture_time) = 0;

  /**
   * Function to grab decoded audio-samples from the media engine for rendering
   * / playout 
   * transport
   * @param speechData: Audio samples buffer of length samples
   * @param samplingFreqHz: Frequency/rate of the sampling 
   * @param capture_delay: Time between reading of the sample to rendering
   * @param lengthSamples: Number of 16 bit samples copied into the buffer
   * NOTE: 1. This function should be invoked every 10 milliseconds for the best
   *          peformance
   */

  virtual MediaConduitErrorCode GetAudioFrame(int16_t speechData[],
                                              uint32_t samplingFreqHz,
                                              uint64_t capture_delay,
                                              unsigned int& lengthSamples) = 0;
  
   /**
    * Function to configure receive codec for teh audio session
    * @param sendSessionConfig: CodecConfiguration
    */

  virtual MediaConduitErrorCode ConfigureSendMediaCodec(const AudioCodecConfig* sendSessionConfig) = 0;

   /**
    * Function to configure receive codec for the audio session
    * @param sendSessionConfig: CodecConfiguration
    */
  virtual MediaConduitErrorCode ConfigureRecvMediaCodec( const AudioCodecConfig* recvSessionConfig) = 0;

};


}

#endif

 



 
