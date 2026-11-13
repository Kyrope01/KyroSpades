"""Optional real AAC/MP4 timing test. Set RECORDER_FFMPEG_DIR to a built FFmpeg tree."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
dir = os.environ.get('RECORDER_FFMPEG_DIR')
if not dir:
    print('Skipping real FFmpeg timing test (set RECORDER_FFMPEG_DIR)')
else:
    ff = Path(dir)
    src = (root/'src/recorder.c').read_text()
    audio = src[src.index('static void recorder_encode_audio('):src.index('\n/* Timestamps are measured')]
    lead = src[src.index('static int64_t recorder_audio_lead_us('):src.index('\nstatic void recorder_free_recording_resources')]
    with tempfile.TemporaryDirectory() as d:
        temp = Path(d)
        (temp/'recorder_generated.h').write_text(audio + '\n' + lead)
        libraries = [ff/f'lib{name}/lib{name}.a' for name in ('avformat','avcodec','swresample','avutil')]
        subprocess.run(['cc','-std=gnu99',f'-I{ff}',f'-I{temp}',str(root/'tests/recorder_ffmpeg_timing.c'),'-Wl,--start-group',*map(str,libraries),'-Wl,--end-group','-lm','-pthread','-o',str(temp/'encode')],check=True)
        subprocess.run([str(temp/'encode'),str(temp/'offset.mp4')],check=True)
        out=subprocess.check_output([str(ff/'ffprobe'),'-v','error','-show_entries','stream=start_time,duration','-of','csv=p=0',str(temp/'offset.mp4')],text=True).strip()
        start,duration=map(float,out.split(','))
        assert abs(start-3.0)<0.002 and 2.0 < duration < 2.1, out
        print('Real FFmpeg AAC/MP4 delayed-audio timing passed:',out)
