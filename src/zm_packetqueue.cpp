//ZoneMinder Packet Queue Implementation Class
//Copyright 2016 Steve Gilvarry
//
//This file is part of ZoneMinder.
//
//ZoneMinder is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.
//
//ZoneMinder is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with ZoneMinder.  If not, see <http://www.gnu.org/licenses/>.


#include "zm_packetqueue.h"
#include "zm_ffmpeg.h"
#include <sys/time.h>

zm_packetqueue::zm_packetqueue( int video_image_count, int p_video_stream_id ) {
  video_stream_id = p_video_stream_id;
  max_video_packet_count = video_image_count-1;
  video_packet_count = 0;
  analysis_it = pktQueue.begin();
}

zm_packetqueue::~zm_packetqueue() {
  clearQueue();
}

bool zm_packetqueue::queuePacket( ZMPacket* zm_packet ) {
	pktQueue.push_back( zm_packet );
  if ( zm_packet->codec_type == AVMEDIA_TYPE_VIDEO ) {
    video_packet_count += 1;
    if ( video_packet_count >= max_video_packet_count ) 
      clearQueue( max_video_packet_count, video_stream_id );
  }

  if ( analysis_it == pktQueue.end() ) {
    // ANalsys_it should only point to end when queue it empty
    Debug(2,"pointing analysis_it to begining");
    analysis_it = pktQueue.begin();
  }

	return true;
}

ZMPacket* zm_packetqueue::popPacket( ) {
	if ( pktQueue.empty() ) {
		return NULL;
	}

	ZMPacket *packet = pktQueue.front();
  if ( *analysis_it == packet )
    analysis_it ++;

	pktQueue.pop_front();
  if ( packet->codec_type == AVMEDIA_TYPE_VIDEO )
    video_packet_count -= 1;

	return packet;
}

unsigned int zm_packetqueue::clearQueue( unsigned int frames_to_keep, int stream_id ) {
  
  Debug(3, "Clearing all but %d frames, queue has %d", frames_to_keep, pktQueue.size() );
  frames_to_keep += 1;

	if ( pktQueue.empty() ) {
    Debug(3, "Queue is empty");
    return 0;
  }

  std::list<ZMPacket *>::reverse_iterator it;
  ZMPacket *packet = NULL;

  for ( it = pktQueue.rbegin(); it != pktQueue.rend() && frames_to_keep; ++it ) {
    ZMPacket *zm_packet = *it;
    AVPacket *av_packet = &(zm_packet->packet);
       
    Debug(4, "Looking at packet with stream index (%d) with keyframe(%d), frames_to_keep is (%d)",
        av_packet->stream_index, zm_packet->keyframe, frames_to_keep );
    
    // Want frames_to_keep video frames.  Otherwise, we may not have enough
    if ( av_packet->stream_index == stream_id ) {
      frames_to_keep --;
    }
  }
  // Might not be starting with a keyframe, but should always start with a keyframe

  if ( frames_to_keep ) {
    Debug(4, "Hit end of queue, still need (%d) video keyframes", frames_to_keep );
  } else {
    if ( it != pktQueue.rend() ) {
      ZMPacket *zm_packet = *it;
      Debug(4, "packet %x %d", zm_packet, zm_packet->image_index);

      AVPacket *av_packet = &(zm_packet->packet);
      while (
          ( it != pktQueue.rend() ) 
          &&
          (( av_packet->stream_index != stream_id ) || !zm_packet->keyframe )
          ) {
        zm_packet = *it;
        //Debug(4, "packet %x %d", zm_packet, zm_packet->image_index);
        ++it;
        av_packet = &( (*it)->packet );
      }
    }
  }
  unsigned int delete_count = 0;
  Debug(4, "Deleting packets from the front, count is (%d)", delete_count );
  while ( it != pktQueue.rend() ) {
    Debug(4, "Deleting a packet from the front, count is (%d)", delete_count );

    packet = pktQueue.front();
    if ( *analysis_it == packet )
      analysis_it ++;
    if ( packet->codec_type == AVMEDIA_TYPE_VIDEO )
      video_packet_count -= 1;
    pktQueue.pop_front();
    if ( packet->image_index == -1 )
      delete packet;

    delete_count += 1;
  } // while our iterator is not the first packet
  Debug(3, "Deleted (%d) packets", delete_count );
  return delete_count; 
} // end unsigned int zm_packetqueue::clearQueue( unsigned int frames_to_keep, int stream_id )

void zm_packetqueue::clearQueue() {
  ZMPacket *packet = NULL;
	while(!pktQueue.empty()) {
    packet = pktQueue.front();
    pktQueue.pop_front();
    if ( packet->image_index == -1 )
      delete packet;
	}
  video_packet_count = 0;
  analysis_it = pktQueue.begin();
}

unsigned int zm_packetqueue::size() {
  return pktQueue.size();
}

unsigned int zm_packetqueue::get_video_packet_count() {
  return video_packet_count;
}

// Returns a packet to analyse or NULL
ZMPacket *zm_packetqueue::get_analysis_packet() {

  if ( ! pktQueue.size() )
    return NULL;
  if ( analysis_it == pktQueue.end() ) 
    return NULL;

  Debug(2, "Distance from head: (%d)", std::distance( pktQueue.begin(), analysis_it ) );
  Debug(2, "Distance from end: (%d)", std::distance( analysis_it, pktQueue.end() ) );

  return *analysis_it;
} // end ZMPacket *zm_packetqueue::get_analysis_packet()

// The idea is that analsys_it will only be == end() if the queue is empty
// probvlem here is that we don't want to analyse a packet twice. Maybe we can flag the packet analysed
bool zm_packetqueue::increment_analysis_it( ) {
  // We do this instead of distance becuase distance will traverse the entire list in the worst case
  std::list<ZMPacket *>::iterator next_it = analysis_it;
  next_it ++;
  if ( next_it == pktQueue.end() ) {
    return false;
  }
  analysis_it = next_it;
  return true;
} // end bool zm_packetqueue::increment_analysis_it( )
