//
// ZoneMinder Remote Camera Class Implementation, $Date$, $Revision$
// Copyright (C) 2001-2008 Philip Coombes
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
// 

#include "zm_remote_camera_nvsocket.h"

#include "zm_mem_utils.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>

#ifdef SOLARIS
#include <sys/filio.h> // FIONREAD and friends
#endif
#ifdef __FreeBSD__
#include <netinet/in.h>
#endif

RemoteCameraNVSocket::RemoteCameraNVSocket(
  unsigned int p_monitor_id,
  const std::string &p_host,
  const std::string &p_port,
  const std::string &p_path,
  int p_width,
  int p_height,
  int p_colours,
  int p_brightness,
  int p_contrast,
  int p_hue,
  int p_colour,
  bool p_capture,
  bool p_record_audio ) :
  RemoteCamera(
    p_monitor_id,
    "http",
    p_host,
    p_port,
    p_path,
    p_width,
    p_height,
    p_colours,
    p_brightness,
    p_contrast,
    p_hue,
    p_colour,
    p_capture,
    p_record_audio )
{
  sd = -1;

  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  subpixelorder = ZM_SUBPIX_ORDER_BGR;
  video_stream = NULL;

  if ( capture ) {
    Initialise();
  }
}

RemoteCameraNVSocket::~RemoteCameraNVSocket() {
  if ( capture ) {
    Terminate();
  }
}

void RemoteCameraNVSocket::Initialise() {
  RemoteCamera::Initialise();

  if ( !timeout.tv_sec ) {
    timeout.tv_sec = config.http_timeout/1000; 
    timeout.tv_usec = (config.http_timeout%1000)*1000;
  }

  int max_size = width*height*colours;

  buffer.size( max_size );

  mode = SINGLE_IMAGE;
  format = UNDEF;
  state = HEADER;
}

int RemoteCameraNVSocket::Connect() {
  int port_num = atoi(port.c_str());
  //struct addrinfo *p;
  struct sockaddr_in servaddr;
  bzero( &servaddr, sizeof(servaddr));
  servaddr.sin_family      = AF_INET;
  servaddr.sin_addr.s_addr = htons(INADDR_ANY);
  servaddr.sin_port        = htons(port_num);

  sd = socket(AF_INET, SOCK_STREAM, 0);
  //for(p = hp; p != NULL; p = p->ai_next) {
  //sd = socket( p->ai_family, p->ai_socktype, p->ai_protocol );
  if ( sd < 0 ) {
    Warning("Can't create socket: %s", strerror(errno) );
    //continue;
    return -1;
  }

  //if ( connect( sd, p->ai_addr, p->ai_addrlen ) < 0 ) {
  if ( connect( sd, (struct sockaddr *)&servaddr , sizeof(servaddr) ) < 0 ) {
    close(sd);
    sd = -1;

    Warning("Can't connect to socket mid: %d : %s", monitor_id, strerror(errno) );
    return -1;
  }

//if ( p == NULL ) {
//Error("Unable to connect to the remote camera, aborting");
//return( -1 );
//}

  Debug( 3, "Connected to host:%d, socket = %d", port_num, sd );
  return sd;
}

int RemoteCameraNVSocket::Disconnect() {
  close( sd );
  sd = -1;
  Debug( 3, "Disconnected from host" );
  return( 0 );
}

int RemoteCameraNVSocket::SendRequest( std::string request ) {
  //Debug( 4, "Sending request: %s", request.c_str() );
  if ( write( sd, request.data(), request.length() ) < 0 ) {
    Error( "Can't write: %s", strerror(errno) );
    Disconnect();
    return( -1 );
  }
  //Debug( 4, "Request sent" );
  return( 0 );
}

int RemoteCameraNVSocket::PrimeCapture() {
  if ( sd < 0 ) {
    Connect();
    if ( sd < 0 ) {
      Error( "Unable to connect to camera" );
      return( -1 );
    }
  }
  buffer.clear();
  struct image_def {
    uint16_t  width;
    uint16_t height;
    uint16_t   type;
  };
  struct image_def image_def;

  if ( SendRequest("GetImageParams\n") < 0 ) {
    Error( "Unable to send request" );
    Disconnect();
    return -1;
  }
  if ( Read(sd, (char *) &image_def, sizeof(image_def)) != sizeof(image_def) ) {
    Error("Unable to GetImageParams");
    Disconnect();
    return -1;
  }
  if ( image_def.width != width || image_def.height != height ) {
    Error("Incorrect width and height set.  %dx%d != %dx%d", width, height, image_def.width, image_def.height );
    Disconnect();
    return -1;
  }
  mVideoStreamId=0;

  return 0;
}

int RemoteCameraNVSocket::Capture( ZMPacket &zm_packet ) {
  if ( SendRequest("GetNextImage\n") < 0 ) {
    Warning( "Unable to capture image, retrying" );
    return 0;
  }
  if ( Read( sd, buffer, imagesize ) < imagesize ) {
    Warning( "Unable to capture image, retrying" );
    return 0;
  }
  uint32_t end;
  if ( Read(sd, (char *) &end , sizeof(end)) < 0 ) {
    Warning( "Unable to capture image, retrying" );
    return 0;
  }
  if ( end != 0xFFFFFFFF) {
    Warning("End Bytes Failed\n");
    return 0;
  }

  zm_packet.image->Assign( width, height, colours, subpixelorder, buffer, imagesize );
  return 1;
}

int RemoteCameraNVSocket::PostCapture() {
  return( 0 );
}
AVStream *RemoteCameraNVSocket::get_VideoStream() {
  if ( ! video_stream ) {
    AVFormatContext *oc = avformat_alloc_context();
    video_stream = avformat_new_stream( oc, NULL );
    if ( video_stream ) {
#if LIBAVCODEC_VERSION_CHECK(57, 64, 0, 64, 0)
      video_stream->codecpar->width = width;
      video_stream->codecpar->height = height;
      video_stream->codecpar->format = GetFFMPEGPixelFormat(colours,subpixelorder);
      video_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
#else
      video_stream->codec->width = width;
      video_stream->codec->height = height;
      video_stream->codec->pix_fmt = GetFFMPEGPixelFormat(colours,subpixelorder);
      video_stream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
#endif
    } else {
      Error("Can't create video stream");
    }
} else {
Debug(2,"Have videostream");
  }
Debug(2,"Get videoStream");
  return video_stream;
}
