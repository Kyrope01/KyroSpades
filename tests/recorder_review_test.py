"""Compile recorder logic against fakes: frame sharing, AAC bounds, and PCM stop."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'src/recorder.c').read_text();enabled=s[s.index('#else /* NO_FFMPEG */'):]
cap=enabled[enabled.index('void recorder_capture_frame(void) {'):enabled.index('\nvoid recorder_shutdown')]
audio=enabled[enabled.index('static void recorder_encode_audio('):enabled.index('\n/* Timestamps are measured')]
cap_stub=r'''
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#define MAX_RAW_FRAMES 8
#define GL_BACK 0
#define GL_RGBA 1
#define GL_UNSIGNED_BYTE 2
#define log_warn(...) ((void)0)
typedef struct {unsigned char*data;int size;int64_t capture_time_us;} RawFrame;
static RawFrame rec_raw_frames[8],raw_frames[8];
static int rec_running=1,buf_running=1,rec_encode_thread_running=1,encode_thread_running=1;
static int rec_buf_w=2,rec_buf_h=2,buf_w=2,buf_h=2,rec_raw_count,rec_raw_tail,raw_count,raw_tail,rec_frames;
static pthread_mutex_t rec_raw_mutex=PTHREAD_MUTEX_INITIALIZER,raw_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rec_raw_cond=PTHREAD_COND_INITIALIZER,raw_cond=PTHREAD_COND_INITIALIZER;
static struct {int window_width,window_height;} settings={2,2};
static double last_capture_time;static int target_fps=60,reads,stops,buffer_stops;
static double window_time(void){static double x;return x+=1;}
static int64_t av_gettime(void){return 55;}
static void glReadBuffer(int b){(void)b;}
static void glReadPixels(int x,int y,int w,int h,int fmt,int type,void*p){(void)x;(void)y;(void)fmt;(void)type;reads++;memset(p,42,w*h*4);}
static void chat_add(int a,int b,const char*c){(void)a;(void)b;(void)c;}
static void audio_notify_events(void){}
static void recorder_stop_recording(void){stops++;rec_running=0;}
static void recorder_buffer_stop(void){buffer_stops++;buf_running=0;}
'''
cap_check=r'''
int main(void){recorder_capture_frame();assert(reads==1&&rec_raw_count==1&&raw_count==1);
 assert(rec_raw_frames[0].data!=raw_frames[0].data&&rec_raw_frames[0].data[0]==42&&raw_frames[0].data[0]==42);
 assert(rec_raw_frames[0].capture_time_us==raw_frames[0].capture_time_us);
 free(rec_raw_frames[0].data);free(raw_frames[0].data);
 rec_raw_count=MAX_RAW_FRAMES;raw_count=MAX_RAW_FRAMES;recorder_capture_frame();assert(reads==2);
 settings.window_width=3;recorder_capture_frame();assert(stops==1&&buffer_stops==1&&reads==2);
 puts("recorder shared read and resize stop passed");}
'''
audio_stub=r'''
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <stdio.h>
#define log_warn(...) ((void)0)
typedef struct {int64_t pts,duration;} AVPacket;
typedef struct {int x;} AVCodecContext;typedef struct {uint8_t*data[1];int nb_samples;int64_t pts;} AVFrame;typedef struct {int x;} SwrContext;
static int pending, allocs,alloc_fail=-1,packets_per_send=1,bad_convert;
int av_frame_make_writable(AVFrame*f){(void)f;return 0;}
int swr_convert(SwrContext*s,uint8_t**o,int n,const uint8_t**i,int count){(void)s;(void)o;(void)n;(void)i;return bad_convert?-1:count;}
int avcodec_send_frame(AVCodecContext*c,AVFrame*f){(void)c;pending=f?packets_per_send:0;return 0;}
AVPacket* av_packet_alloc(void){if(allocs++==alloc_fail)return NULL;return calloc(1,sizeof(AVPacket));}
int avcodec_receive_packet(AVCodecContext*c,AVPacket*p){(void)c;assert(p);if(!pending)return -1;pending--;p->pts=allocs;return 0;}
void av_packet_free(AVPacket**p){free(*p);*p=NULL;}
'''
audio_check=r'''
int main(void){AVCodecContext c={0};AVFrame f={.nb_samples=1024};SwrContext sw={0};int16_t pcm[2048]={0};
 AVPacket**p=NULL;int64_t*pts=NULL;int n=0;packets_per_send=10;
 recorder_encode_audio(&c,&f,&sw,pcm,1,&p,&pts,&n);assert(n==2);for(int i=0;i<n;i++)av_packet_free(&p[i]);free(p);free(pts);
 alloc_fail=allocs;recorder_encode_audio(&c,&f,&sw,pcm,1,&p,&pts,&n);assert(n==0);free(p);free(pts);
 alloc_fail=-1;bad_convert=1;recorder_encode_audio(&c,&f,&sw,pcm,1,&p,&pts,&n);assert(n==0);free(p);free(pts);
 puts("recorder audio allocation and packet bounds passed");}
'''
with tempfile.TemporaryDirectory() as d:
 for name,code in [('capture',cap_stub+cap+cap_check),('audio',audio_stub+audio+audio_check)]:
  p=Path(d);(p/(name+'.c')).write_text(code)
  subprocess.run(['cc','-std=gnu99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/(name+'.c')),'-pthread','-o',str(p/name)],check=True)
  subprocess.run([str(p/name)],check=True)
assert 'while(pkt && avcodec_receive_packet(rec_venc_ctx, pkt)' in enabled
assert 'while(pkt && avcodec_receive_packet(venc_ctx, pkt)' in enabled
assert 'avcodec_parameters_copy(vs->codecpar, buf_codecpar)' in enabled
assert 'if(!buf_running || !buf_codecpar)' in enabled
assert 'int mux_a_count = a_count;' in enabled
assert 'int idx = (oldest_idx(vbuf_write_idx, vbuf_count) + i) % MAX_PACKETS;' in enabled
# Run the actual PulseAudio worker with a read paused across replay stop.
worker=enabled[enabled.index('static void* audio_capture_thread(void* arg) {'):enabled.index('\n/* ═════════',enabled.index('static void* audio_capture_thread(void* arg) {'))]
worker_stub=r'''
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#define USE_PULSEAUDIO 1
#define log_warn(...) ((void)0)
#define PA_SAMPLE_S16LE 1
#define PA_STREAM_RECORD 1
typedef struct {int format,rate,channels;} pa_sample_spec;typedef int pa_simple;
static pthread_mutex_t pcm_mutex=PTHREAD_MUTEX_INITIALIZER,rec_pcm_mutex=PTHREAD_MUTEX_INITIALIZER,audio_state_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
static int ready,proceed,reads,audio_thread_running=1,buf_running=1,rec_running=1;
static unsigned char* pcm_buf;static int pcm_write_byte,pcm_bytes,pcm_max_bytes=8192;
static int16_t*rec_pcm_buf;static int rec_pcm_frames,rec_pcm_capacity=48000,rec_pcm_truncated;
static int64_t rec_audio_start_us,rec_start_time=1000000;
#define AUDIO_EVENT_CAPTURE_LOST 1
#define AUDIO_EVENT_PCM_FULL 2
static int reported;
static void audio_report_event(unsigned e){reported |= e;}
static int64_t av_gettime(void){return 1100000;}
static int fail_grow;
static void* test_realloc(void*p,size_t n){return fail_grow ? NULL : realloc(p,n);}
#define realloc test_realloc
static struct {char audio_monitor_source[16];} settings;
static int audio_is_running(void){pthread_mutex_lock(&audio_state_mutex);int a=audio_thread_running;pthread_mutex_unlock(&audio_state_mutex);return a;}
static pa_simple* pa_simple_new(void*a,const char*b,int c,const char*d,const char*e,const pa_sample_spec*f,void*g,void*h,int*i){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;static int x;return &x;}
#define pa_strerror(i) "error"
static int pa_simple_read(pa_simple*s,void*buf,int n,void*e){(void)s;(void)e;if(reads++)return -1;pthread_mutex_lock(&gate);ready=1;pthread_cond_signal(&cond);while(!proceed)pthread_cond_wait(&cond,&gate);pthread_mutex_unlock(&gate);memset(buf,7,n);return 0;}
static void pa_simple_free(pa_simple*s){(void)s;}
'''
worker_check=r'''
int main(void){pcm_buf=malloc(pcm_max_bytes);rec_pcm_buf=malloc((size_t)rec_pcm_capacity*4);assert(pcm_buf&&rec_pcm_buf);
 pthread_t t;assert(!pthread_create(&t,NULL,audio_capture_thread,NULL));pthread_mutex_lock(&gate);while(!ready)pthread_cond_wait(&cond,&gate);pthread_mutex_unlock(&gate);
 pthread_mutex_lock(&pcm_mutex);buf_running=0;free(pcm_buf);pcm_buf=NULL;pthread_mutex_unlock(&pcm_mutex);
 pthread_mutex_lock(&gate);proceed=1;pthread_cond_signal(&cond);pthread_mutex_unlock(&gate);pthread_join(t,NULL);
 assert(rec_pcm_frames==1024&&pcm_buf==NULL&&pcm_bytes==0);assert(rec_audio_start_us==1078667 && (reported&AUDIO_EVENT_CAPTURE_LOST));
 int16_t*old=rec_pcm_buf;rec_pcm_capacity=1024;reads=0;ready=0;proceed=1;fail_grow=1;reported=0;
 assert(!pthread_create(&t,NULL,audio_capture_thread,NULL));pthread_join(t,NULL);
 assert(rec_pcm_buf==old&&rec_pcm_frames==1024&&rec_pcm_truncated&&(reported&AUDIO_EVENT_PCM_FULL));
 free(rec_pcm_buf);puts("audio replay-stop overlap and PCM failure passed");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'worker.c').write_text(worker_stub+worker+worker_check)
 subprocess.run(['cc','-std=gnu99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'worker.c'),'-pthread','-o',str(p/'worker')],check=True)
 subprocess.run([str(p/'worker')],check=True)

lead=enabled[enabled.index('static int64_t recorder_audio_lead_us('):enabled.index('\nstatic void recorder_free_recording_resources')]
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'lead.c').write_text('#include <stdint.h>\n#include <assert.h>\n'+lead+r'''
int main(void) {assert(recorder_audio_lead_us(4000000,1000000)==3000000);assert(recorder_audio_lead_us(900000,1000000)==0);assert(recorder_audio_lead_us(0,1000000)==0);}
''')
 subprocess.run(['cc','-std=gnu99','-Wall','-Wextra','-Werror',str(p/'lead.c'),'-o',str(p/'lead')],check=True);subprocess.run([str(p/'lead')],check=True)
assert 'a_pts_arr[a_written] - a_pts_offset + audio_lead' in enabled
