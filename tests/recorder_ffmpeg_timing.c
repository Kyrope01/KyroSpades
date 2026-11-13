#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#define log_warn(...) fprintf(stderr, __VA_ARGS__)
#include "recorder_generated.h"

int main(int argc,char**argv) {
 assert(argc==2); const AVCodec* codec=avcodec_find_encoder(AV_CODEC_ID_AAC); assert(codec);
 AVCodecContext* enc=avcodec_alloc_context3(codec); assert(enc);enc->sample_fmt=AV_SAMPLE_FMT_FLTP;enc->sample_rate=48000;enc->time_base=(AVRational){1,48000};enc->ch_layout=(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;enc->bit_rate=192000;assert(avcodec_open2(enc,codec,NULL)>=0);
 AVFrame* f=av_frame_alloc();assert(f);f->format=AV_SAMPLE_FMT_FLTP;f->sample_rate=48000;f->ch_layout=(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;f->nb_samples=1024;assert(av_frame_get_buffer(f,0)>=0);
 SwrContext* sw=NULL;assert(swr_alloc_set_opts2(&sw,&(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO,AV_SAMPLE_FMT_FLTP,48000,&(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO,AV_SAMPLE_FMT_S16,48000,0,NULL)>=0);assert(swr_init(sw)>=0);
 int frames=94,count=0;int16_t*pcm=calloc((size_t)frames*2048,sizeof(int16_t));assert(pcm);AVPacket**pkts=NULL;int64_t*pts=NULL;recorder_encode_audio(enc,f,sw,pcm,frames,&pkts,&pts,&count);assert(count>90);
 AVFormatContext* oc=NULL;assert(avformat_alloc_output_context2(&oc,NULL,"mp4",argv[1])>=0);AVStream* st=avformat_new_stream(oc,NULL);assert(st);assert(avcodec_parameters_from_context(st->codecpar,enc)>=0);st->time_base=enc->time_base;assert(avio_open(&oc->pb,argv[1],AVIO_FLAG_WRITE)>=0);assert(avformat_write_header(oc,NULL)>=0);
 int64_t off=av_rescale_q(recorder_audio_lead_us(4000000,1000000),AV_TIME_BASE_Q,enc->time_base),base=pts[0];
 for(int i=0;i<count;i++){ AVPacket out={0};assert(av_packet_ref(&out,pkts[i])>=0);out.pts=av_rescale_q(pts[i]-base+off,enc->time_base,st->time_base);out.dts=out.pts;out.duration=av_rescale_q(pkts[i]->duration,enc->time_base,st->time_base);out.stream_index=0;assert(av_interleaved_write_frame(oc,&out)>=0);}
 assert(av_write_trailer(oc)>=0);avio_closep(&oc->pb);avformat_free_context(oc);for(int i=0;i<count;i++)av_packet_free(&pkts[i]);free(pkts);free(pts);free(pcm);av_frame_free(&f);avcodec_free_context(&enc);swr_free(&sw);return 0;
}
