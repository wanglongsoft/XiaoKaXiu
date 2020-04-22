#include <jni.h>
#include <string>
#include <android/log.h>
#include <string>

#define TAG "XiaoKaXiu"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

extern "C" {
    #include "libavutil/avutil.h"
    #include "libavformat/avformat.h"
    #include "libavutil/intreadwrite.h"
    #include "libavutil/timestamp.h"
}

int audioVideoClip(const char * src_av_file, const char * dst_av_file, int start_time, int stop_time);
int audioFileExact(const char * src_clip_file, const char * dst_aac_file);
int videoFileExact(const char * src_clip_file, const char * dst_h264_file);
int audioVideoFileMerge(const char * src_aac_file, const char * src_h264_file, const char * dst_av_file);

void adts_header(char *szAdtsHeader, int dataLen);
static int alloc_and_copy(AVPacket *out,
                          const uint8_t *sps_pps, uint32_t sps_pps_size,
                          const uint8_t *in, uint32_t in_size);
int h264_mp4toannexb(AVFormatContext *fmt_ctx, AVPacket *in, FILE *dst_fd);

int h264_extradata_to_annexb(const uint8_t *codec_extradata, const int codec_extradata_size, AVPacket *out_extradata, int padding);


//src_audio_file: mp4, src_video_file: mp4
extern "C"
JNIEXPORT void JNICALL
Java_com_wl_xiaokaxiu_MainActivity_mergeAudioVideoFile(JNIEnv *env, jobject thiz,
                                                       jstring src_audio_file,
                                                       jstring src_video_file,
                                                       jstring dst_av_file,
                                                       jint audio_start_time, jint audio_stop_time,
                                                       jint video_start_time,
                                                       jint video_stop_time) {
    LOGD("mergeAudioVideoFile in");
    const char * srcAudioFile = env->GetStringUTFChars(src_audio_file, NULL);
    const char * srcVideoFile = env->GetStringUTFChars(src_video_file, NULL);
    const char * dstAVFile = env->GetStringUTFChars(dst_av_file, NULL);
    int audioStartTime = audio_start_time;
    int audioStopTime = audio_stop_time;
    int videoStartTime = video_start_time;
    int videoStopTime = video_stop_time;
    int ret = -1;

    //定义中间文件名 start
    const char * dst_audio_clip_file = "/storage/emulated/0/filefilm/xiaokaxiu_clip_audio.mp4";
    const char * dst_video_clip_file = "/storage/emulated/0/filefilm/xiaokaxiu_clip_video.mp4";
    const char * dst_audio_exact_file = "/storage/emulated/0/filefilm/xiaokaxiu_audio_exact.aac";
    const char * dst_video_exact_file = "/storage/emulated/0/filefilm/xiaokaxiu_video_exact.h264";
    //定义中间文件名 end

    //1. 音视频源文件裁剪
    ret = audioVideoClip(srcAudioFile, dst_audio_clip_file, audioStartTime, audioStopTime);
    if(ret != 0) {
        LOGD("audioVideoClip audio fail");
        goto end;
    }
    ret = audioVideoClip(srcVideoFile, dst_video_clip_file, video_start_time, video_stop_time);
    if(ret != 0) {
        LOGD("audioVideoClip video fail");
        goto end;
    }
    //2. 音频抽离，视频抽离
    ret = audioFileExact(dst_audio_clip_file, dst_audio_exact_file);
    if(ret != 0) {
        LOGD("audioFileExact fail");
        goto end;
    }
    ret = videoFileExact(dst_video_clip_file, dst_video_exact_file);
    if(ret != 0) {
        LOGD("videoFileExact fail");
        goto end;
    }
    //3. 音视频合并
    ret = audioVideoFileMerge(dst_audio_exact_file, dst_video_exact_file, dstAVFile);
    if(ret != 0) {
        LOGD("audioVideoFileMerge fail");
        goto end;
    }
    //4. 删除中间文件
    avpriv_io_delete(dst_audio_clip_file);
    avpriv_io_delete(dst_video_clip_file);
    avpriv_io_delete(dst_audio_exact_file);
    avpriv_io_delete(dst_video_exact_file);

    end:
    env->ReleaseStringUTFChars(src_audio_file, srcAudioFile);
    env->ReleaseStringUTFChars(src_video_file, srcVideoFile);
    env->ReleaseStringUTFChars(dst_av_file, dstAVFile);
    LOGD("mergeAudioVideoFile out");
}

int audioVideoClip(const char * src_av_file, const char * dst_av_file, int start_time, int stop_time) {
    int starttime = start_time;
    int stoptime = stop_time;
    LOGD("audioVideoClip in: %d, %d", starttime, stoptime);
    const char * src_file = src_av_file;
    const char * dst_file = dst_av_file;
    int64_t *dts_start_from, *pts_start_from;

    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL;
    AVFormatContext *ofmt_ctx = NULL;
    AVPacket pkt;
    char errors[1024];
    int ret;
    int result = -1;

    ifmt_ctx = avformat_alloc_context();
    if(!ifmt_ctx) {
        LOGD("ifmt_ctx alloc fail");
        goto end;
    }

    ofmt_ctx = avformat_alloc_context();
    if(!ofmt_ctx) {
        LOGD("ofmt_ctx alloc fail");
        goto end;
    }

    // 打开输入文件为ifmt_ctx分配内存
    if(avformat_open_input(&ifmt_ctx, src_file, NULL, NULL)) {
        LOGD("input file %s open fail", src_file);
        goto end;
    }

    // 检索输入文件的流信息
    if(avformat_find_stream_info(ifmt_ctx, NULL)) {
        LOGD("find stream info fail");
        goto end;
    }

    ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, dst_file);

    if(!ofmt_ctx) {
        av_strerror(ret, errors, 1024);
        LOGD("avformat_alloc_output_context2 fail : %s", errors);
        goto end;
    }

    ofmt = ofmt_ctx->oformat;

    LOGD("ifmt_ctx->nb_streams : %d", ifmt_ctx->nb_streams);
    for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            LOGD("Failed allocating output stream");
            goto end;
        }

        // 直接将输入流的编解码参数拷贝到输出流中
        if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) {
            LOGD("avcodec_parameters_copy fail");
            goto end;
        }

        out_stream->codecpar->codec_tag = 0;

    }

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt_ctx->pb, dst_file, AVIO_FLAG_WRITE) < 0) {
            LOGD("Could not open output file '%s'", dst_file);
            goto end;
        }
    }

    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        LOGD("Error occurred when opening output file");
        goto end;
    }

    ret = av_seek_frame(ifmt_ctx, -1, starttime * AV_TIME_BASE, AVSEEK_FLAG_ANY);
    if (ret < 0) {
        LOGD("Error seek");
        goto end;
    }

    dts_start_from = (int64_t *) malloc(sizeof(int64_t) *ifmt_ctx->nb_streams);
    memset(dts_start_from, 0, sizeof(int64_t) * ifmt_ctx->nb_streams);
    pts_start_from = (int64_t *) malloc(sizeof(int64_t) *ifmt_ctx->nb_streams);
    memset(pts_start_from, 0, sizeof(int64_t) * ifmt_ctx->nb_streams);

    while (1) {
        AVStream *in_stream, *out_stream;
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {// 读取完后退出循环
                LOGD("read pkt complete");
            } else {
                LOGD("read pkt fail");
            }
            break;
        }

        in_stream = ifmt_ctx->streams[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];
//        LOGD("process time : %lf", av_q2d(in_stream->time_base) * pkt.pts);
        if (av_q2d(in_stream->time_base) * pkt.pts > stoptime) {
            av_packet_unref(&pkt);
            LOGD("time > stop, break");
            break;
        }

        if (dts_start_from[pkt.stream_index] == 0) {
            dts_start_from[pkt.stream_index] = pkt.dts;
        }
        if (pts_start_from[pkt.stream_index] == 0) {
            pts_start_from[pkt.stream_index] = pkt.pts;
            LOGD("pts_start_from :%d", pkt.pts);
        }

        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                   (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                   (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.duration = (int) av_rescale_q((int64_t) pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        if (pkt.pts < 0) {
            pkt.pts = 0;
        }
        if (pkt.dts < 0) {
            pkt.dts = 0;
        }

        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);

        if (ret < 0) {
            av_strerror(ret, errors, 1024);
            LOGD("av_interleaved_write_frame fail : %s", errors);
            goto end;
        }
        av_packet_unref(&pkt);
    }

    av_write_trailer(ofmt_ctx);

    result = 0;
    end:
    if(NULL != ifmt_ctx) {
        avformat_close_input(&ifmt_ctx);
        avformat_free_context(ifmt_ctx);
    }
    if(NULL != ofmt_ctx) {
        if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
            avio_closep(&ofmt_ctx->pb);
        avformat_free_context(ofmt_ctx);
    }

    free(dts_start_from);
    free(pts_start_from);

    LOGD("audioVideoClip out");
    return result;
}

int audioFileExact(const char * src_clip_file, const char * dst_aac_file) {
    LOGD("audioExtraction in");
    const char * src_file = src_clip_file;
    const char * dst_file = dst_aac_file;
    FILE *dst_fd = NULL;
    AVFormatContext *fmt_ctx;
    AVPacket pkt;
    int err_code;
    char errors[1024];
    int audio_stream_index = -1;
    int len;
    int result = -1;

    dst_fd = fopen(dst_file, "wb");
    if(!dst_fd) {
        LOGD("fopen file : %s fail", dst_file);
        goto end;
    }

    fmt_ctx = avformat_alloc_context();

    if((err_code = avformat_open_input(&fmt_ctx, src_file, NULL, NULL)) < 0){
        av_strerror(err_code, errors, 1024);
        LOGD("Could not open source file: %s, %d(%s)", src_file,
             err_code,
             errors);
        goto end;
    }

    audio_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

    if(audio_stream_index < 0){
        av_strerror(err_code, errors, 1024);
        LOGD("Could not find %s stream in input file %s", av_get_media_type_string(AVMEDIA_TYPE_AUDIO), src_file);
        goto end;
    }

    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    while(av_read_frame(fmt_ctx, &pkt) >= 0) {
        if(pkt.stream_index == audio_stream_index){

            char adts_header_buf[7];
            adts_header(adts_header_buf, pkt.size);
            fwrite(adts_header_buf, 1, 7, dst_fd);

            len = fwrite(pkt.data, 1, pkt.size, dst_fd);
            if(len != pkt.size){
                LOGD("warning, length of writed data isn't equal pkt.size(%d, %d)\n", len, pkt.size);
            }
        }
        av_packet_unref(&pkt);
    }

    result = 0;
    end:
    avformat_close_input(&fmt_ctx);
    fflush(dst_fd);
    fclose(dst_fd);
    avformat_free_context(fmt_ctx);
    LOGD("audioExtraction out");
    return result;
}

int videoFileExact(const char * src_clip_file, const char * dst_h264_file) {
    LOGD("videoExtraction in");

    const char * src_file = src_clip_file;
    const char * dst_file = dst_h264_file;
    FILE *dst_fd = NULL;
    AVFormatContext *fmt_ctx;
    AVPacket pkt;
    int err_code;
    char errors[1024];
    int video_stream_index = -1;
    int len;
    int result = -1;

    dst_fd = fopen(dst_file, "wb");
    if(!dst_fd) {
        LOGD("fopen file : %s fail", dst_file);
        goto end;
    }

    fmt_ctx = avformat_alloc_context();

    if((err_code = avformat_open_input(&fmt_ctx, src_file, NULL, NULL)) < 0){
        av_strerror(err_code, errors, 1024);
        LOGD("Could not open source file: %s, %d(%s)", src_file,
             err_code,
             errors);
        goto end;
    }

    video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

    if(video_stream_index < 0){
        av_strerror(err_code, errors, 1024);
        LOGD("Could not find %s stream in input file %s", av_get_media_type_string(AVMEDIA_TYPE_VIDEO), src_file);
        goto end;

    }

    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    while(av_read_frame(fmt_ctx, &pkt) >= 0) {
        if (pkt.stream_index == video_stream_index) {
            h264_mp4toannexb(fmt_ctx, &pkt, dst_fd);
        }
        av_packet_unref(&pkt);
    }

    result = 0;
    end:
    avformat_close_input(&fmt_ctx);
    fflush(dst_fd);
    fclose(dst_fd);
    avformat_free_context(fmt_ctx);
    LOGD("videoExtraction out");
    return result;
}

int audioVideoFileMerge(const char * src_aac_file, const char * src_h264_file, const char * dst_av_file) {
    LOGD("mergeAudioVideoFile in");
    const char * srcAudioFile = src_aac_file;
    const char * srcVideoFile = src_h264_file;
    const char * dstAVFile = dst_av_file;
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *iafmt_ctx = NULL;
    AVFormatContext *ivfmt_ctx = NULL;
    AVFormatContext *ofmt_ctx = NULL;

    AVPacket audio_pkt;
    AVPacket video_pkt;
    char errors[1024];
    int ret;
    int result = -1;
    int video_packet_count = 0;

    AVStream *video_in_stream = NULL;
    AVStream *video_out_stream = NULL;
    AVStream *audio_in_stream = NULL;
    AVStream *audio_out_stream = NULL;
    int out_video_stream_index = -1;
    int out_audio_stream_index = -1;
    LOGD("srcAudioFile : %s", srcAudioFile);
    LOGD("srcVideoFile : %s", srcVideoFile);
    LOGD("dstAVFile : %s", dstAVFile);

    // 打开音频输入文件为iafmt_ctx分配内存
    if(avformat_open_input(&iafmt_ctx, srcAudioFile, NULL, NULL)) {
        LOGD("input file %s open fail", srcAudioFile);
        goto end;
    }

    // 打开视频输入文件为ivfmt_ctx分配内存
    if(avformat_open_input(&ivfmt_ctx, srcVideoFile, NULL, NULL)) {
        LOGD("input file %s open fail", srcVideoFile);
        goto end;
    }

    ret = avformat_find_stream_info(iafmt_ctx, NULL);
    if(ret < 0) {
        LOGD("avformat_find_stream_info fail : iafmt_ctx");
        goto end;
    }

    ret = avformat_find_stream_info(ivfmt_ctx, NULL);
    if(ret < 0) {
        LOGD("avformat_find_stream_info fail : ivfmt_ctx");
        goto end;
    }

    ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, dstAVFile);

    if(!ofmt_ctx) {
        av_strerror(ret, errors, 1024);
        LOGD("avformat_alloc_output_context2 fail : %s", errors);
        goto end;
    }

    if(ivfmt_ctx->nb_streams == 1) {
        //创建视频输出流
        LOGD("create video stream");
        video_in_stream = ivfmt_ctx->streams[0];
        video_out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!video_out_stream) {
            LOGD("Failed allocating output stream");
            goto end;
        }

        // 直接将输入流的编解码参数拷贝到输出流中
        if (avcodec_parameters_copy(video_out_stream->codecpar, video_in_stream->codecpar) < 0) {
            LOGD("avcodec_parameters_copy fail");
            goto end;
        }

        video_out_stream->codecpar->codec_tag = 0;
        LOGD("video_in_stream->codecpar->codec_type : %d", video_in_stream->codecpar->codec_type);
    }

    if(iafmt_ctx->nb_streams == 1) {
        //创建音频输出流
        LOGD("create audio stream");
        audio_in_stream = iafmt_ctx->streams[0];
        audio_out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!audio_out_stream) {
            LOGD("Failed allocating output stream");
            goto end;
        }

        // 直接将输入流的编解码参数拷贝到输出流中
        if (avcodec_parameters_copy(audio_out_stream->codecpar, audio_in_stream->codecpar) < 0) {
            LOGD("avcodec_parameters_copy fail");
            goto end;
        }
        audio_out_stream->codecpar->codec_tag = 0;
        LOGD("audio_in_stream->codecpar->codec_type : %d", audio_in_stream->codecpar->codec_type);
    }

    ofmt = ofmt_ctx->oformat;

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt_ctx->pb, dstAVFile, AVIO_FLAG_WRITE) < 0) {
            LOGD("Could not open output file '%s'", dstAVFile);
            goto end;
        }
    }

    for(int i = 0; i < ofmt_ctx->nb_streams; i++) {
        if(ofmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            out_audio_stream_index = i;
        } else if(ofmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            out_video_stream_index = i;
        } else {
            LOGD("unknown stream");
        }
    }

    LOGD("out_audio_stream_index : %d out_video_stream_index : %d", out_audio_stream_index, out_video_stream_index);
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        LOGD("Error occurred when avformat_write_header");
        goto end;
    }

    av_init_packet(&audio_pkt);
    av_init_packet(&video_pkt);

    //拷贝音频压缩数据
    while (1) {
        ret = av_read_frame(iafmt_ctx, &audio_pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {// 读取完后退出循环
                LOGD("read audio pkt complete");
            } else {
                LOGD("read audio pkt fail");
            }
            break;
        }

        /* copy packet */
        audio_pkt.pts = av_rescale_q_rnd(audio_pkt.pts, audio_in_stream->time_base, audio_out_stream->time_base,
                                         (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        audio_pkt.dts = av_rescale_q_rnd(audio_pkt.dts, audio_in_stream->time_base, audio_out_stream->time_base,
                                         (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        audio_pkt.duration = (int) av_rescale_q((int64_t) audio_pkt.duration, audio_in_stream->time_base, audio_out_stream->time_base);
        audio_pkt.pos = -1;
        audio_pkt.stream_index = out_audio_stream_index;

        if (audio_pkt.pts < 0) {
            audio_pkt.pts = 0;
        }
        if (audio_pkt.dts < 0) {
            audio_pkt.dts = 0;
        }

        ret = av_interleaved_write_frame(ofmt_ctx, &audio_pkt);

        if (ret < 0) {
            av_strerror(ret, errors, 1024);
            LOGD("av_interleaved_write_frame audio fail : %s", errors);
            goto end;
        }
        av_packet_unref(&audio_pkt);
    }

    //拷贝视频压缩数据
    while (1) {
        ret = av_read_frame(ivfmt_ctx, &video_pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {// 读取完后退出循环
                LOGD("read video pkt complete cnt : %d", video_packet_count);
            } else {
                LOGD("read video pkt fail");
            }
            break;
        }

        if(video_pkt.pts == AV_NOPTS_VALUE) {//添加PTS,DTS信息
            AVRational time_base2 = video_in_stream->time_base;
            int64_t calc_duration = (double) AV_TIME_BASE / av_q2d(video_in_stream->r_frame_rate);
            video_pkt.pts = (double)(video_packet_count * calc_duration) / (double)(av_q2d(time_base2) * AV_TIME_BASE);
            video_pkt.dts = video_pkt.pts;
            video_pkt.duration = (double)calc_duration / (double)(av_q2d(time_base2) * AV_TIME_BASE);
            video_packet_count++;
        }

        /* copy packet */
        video_pkt.pts = av_rescale_q_rnd(video_pkt.pts, video_in_stream->time_base, video_out_stream->time_base,
                                         (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        video_pkt.dts = av_rescale_q_rnd(video_pkt.dts, video_in_stream->time_base, video_out_stream->time_base,
                                         (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        video_pkt.duration = (int) av_rescale_q((int64_t) video_pkt.duration, video_in_stream->time_base, video_out_stream->time_base);
        video_pkt.pos = -1;
        video_pkt.stream_index = out_video_stream_index;//必须设置，否则视频不显示

        if (video_pkt.pts < 0) {
            video_pkt.pts = 0;
        }
        if (video_pkt.dts < 0) {
            video_pkt.dts = 0;
        }

        ret = av_interleaved_write_frame(ofmt_ctx, &video_pkt);

        if (ret < 0) {
            av_strerror(ret, errors, 1024);
            LOGD("av_interleaved_write_frame video fail : %s", errors);
            goto end;
        }
        av_packet_unref(&video_pkt);
    }

    ret = av_write_trailer(ofmt_ctx);
    if (ret != 0) {
        LOGD("Error av_write_trailer");
        goto end;
    }

    result = 0;

    end:
    if(NULL != iafmt_ctx) {
        avformat_close_input(&iafmt_ctx);
        avformat_free_context(iafmt_ctx);
    }
    if(NULL != ivfmt_ctx) {
        avformat_close_input(&ivfmt_ctx);
        avformat_free_context(ivfmt_ctx);
    }

    if(NULL != ofmt_ctx) {
        avformat_free_context(ofmt_ctx);
    }
    LOGD("audioVideoFileMerge out");
    return result;
}

void adts_header(char *szAdtsHeader, int dataLen) {
    int aac_type = 1;
    // 采样率下标，下标7表示采样率为22050
    int sampling_frequency_index = 7;
    // 声道数
    int channel_config = 2;

    // ADTS帧长度,包括ADTS长度和AAC声音数据长度的和。
    int adtsLen = dataLen + 7;

    // syncword,标识一个帧的开始，固定为0xFFF,占12bit(byte0占8位,byte1占前4位)
    szAdtsHeader[0] = 0xff;
    szAdtsHeader[1] = 0xf0;

    // ID,MPEG 标示符。0表示MPEG-4，1表示MPEG-2。占1bit(byte1第5位)
    szAdtsHeader[1] |= (0 << 3);

    // layer,固定为0，占2bit(byte1第6、7位)
    szAdtsHeader[1] |= (0 << 1);

    // protection_absent，标识是否进行误码校验。0表示有CRC校验，1表示没有CRC校验。占1bit(byte1第8位)
    szAdtsHeader[1] |= 1;

    // profile,标识使用哪个级别的AAC。1: AAC Main 2:AAC LC 3:AAC SSR 4:AAC LTP。占2bit(byte2第1、2位)
    szAdtsHeader[2] = aac_type<<6;

    // sampling_frequency_index,采样率的下标。占4bit(byte2第3、4、5、6位)
    szAdtsHeader[2] |= (sampling_frequency_index & 0x0f)<<2;

    // private_bit,私有位，编码时设置为0，解码时忽略。占1bit(byte2第7位)
    szAdtsHeader[2] |= (0 << 1);

    // channel_configuration,声道数。占3bit(byte2第8位和byte3第1、2位)
    szAdtsHeader[2] |= (channel_config & 0x04)>>2;
    szAdtsHeader[3] = (channel_config & 0x03)<<6;

    // original_copy,编码时设置为0，解码时忽略。占1bit(byte3第3位)
    szAdtsHeader[3] |= (0 << 5);

    // home,编码时设置为0，解码时忽略。占1bit(byte3第4位)
    szAdtsHeader[3] |= (0 << 4);

    // copyrighted_id_bit,编码时设置为0，解码时忽略。占1bit(byte3第5位)
    szAdtsHeader[3] |= (0 << 3);

    // copyrighted_id_start,编码时设置为0，解码时忽略。占1bit(byte3第6位)
    szAdtsHeader[3] |= (0 << 2);

    // aac_frame_length,ADTS帧长度,包括ADTS长度和AAC声音数据长度的和。占13bit(byte3第7、8位，byte4全部，byte5第1-3位)
    szAdtsHeader[3] |= ((adtsLen & 0x1800) >> 11);
    szAdtsHeader[4] = (uint8_t)((adtsLen & 0x7f8) >> 3);
    szAdtsHeader[5] = (uint8_t)((adtsLen & 0x7) << 5);

    // adts_buffer_fullness，固定为0x7FF。表示是码率可变的码流 。占11bit(byte5后5位，byte6前6位)
    szAdtsHeader[5] |= 0x1f;
    szAdtsHeader[6] = 0xfc;

    // number_of_raw_data_blocks_in_frame,值为a的话表示ADST帧中有a+1个原始帧，(一个AAC原始帧包含一段时间内1024个采样及相关数据)。占2bit（byte6第7、8位）。
    szAdtsHeader[6] |= 0;
}

/*
 在帧前面添加特征码(一般SPS/PPS的帧的特征码用4字节表示，为0X00000001，其他的帧特征码用3个字节表示，为0X000001。也有都用4字节表示的，我们这里采用4字节的方式)
 out是要输出的AVPaket
 sps_pps是SPS和PPS数据的指针，对于非关键帧就传NULL
 sps_pps_size是SPS/PPS数据的大小，对于非关键帧传0
 in是指向当前要处理的帧的头信息的指针
 in_size是当前要处理的帧大小(nal_size)
*/
static int alloc_and_copy(AVPacket *out,
                          const uint8_t *sps_pps, uint32_t sps_pps_size,
                          const uint8_t *in, uint32_t in_size)
{
    uint32_t offset = out->size;// 偏移量，就是out已有数据的大小，后面再写入数据就要从偏移量处开始操作
    uint8_t nal_header_size = 4;// 特征码的大小，SPS/PPS占4字节，其余占3字节
    int err;

    // 每次处理前都要对out进行扩容，扩容的大小就是此次要写入的内容的大小，也就是特征码大小加上sps/pps大小加上加上本帧数据大小
    err = av_grow_packet(out, sps_pps_size + in_size + nal_header_size);
    if (err < 0)
        return err;

    // 1.如果有sps_pps则先将sps_pps拷贝进out（memcpy()函数用于内存拷贝，第一个参数为拷贝要存储的地方，第二个参数是要拷贝的内容，第三个参数是拷贝内容的大小）
    if (sps_pps) {
        memcpy(out->data + offset, sps_pps, sps_pps_size);
    }

    // 2.再设置特征码

    for (int i = 0; i < nal_header_size; i++)
    {
        (out->data + offset + sps_pps_size)[i] = i == nal_header_size - 1 ? 1 : 0;
    }

    //3. 写入原包数据
    memcpy(out->data + offset + sps_pps_size + nal_header_size, in, in_size);

    return 0;
}

/*
读取并拷贝sps/pps数据
codec_extradata是codecpar的扩展数据，sps/pps数据就在这个扩展数据里面
codec_extradata_size是扩展数据大小
out_extradata是输出sps/pps数据的AVPacket包
padding:就是宏AV_INPUT_BUFFER_PADDING_SIZE的值(64)，是用于解码的输入流的末尾必要的额外字节个数，需要它主要是因为一些优化的流读取器一次读取32或者64比特，可能会读取超过size大小内存的末尾。
*/
int h264_extradata_to_annexb(const uint8_t *codec_extradata, const int codec_extradata_size, AVPacket *out_extradata, int padding)
{
    uint16_t unit_size = 0; // sps或者pps数据长度
    uint64_t total_size = 0; // 所有sps或者pps数据长度加上其特征码长度后的总长度

    /*
        out:是一个指向一段内存的指针，这段内存用于存放所有拷贝的sps或者pps数据和其特征码数据
        unit_nb:sps/pps个数
        sps_done：sps数据是否已经处理完毕
        sps_seen：是否有sps数据
        pps_seen：是否有pps数据
        sps_offset：sps数据的偏移，为0
        pps_offset：pps数据的偏移，因为pps数据在sps后面，所以其偏移就是所有sps数据长度+sps的特征码所占字节数
    */

    uint8_t *out = NULL, unit_nb, sps_done = 0,
            sps_seen                   = 0, pps_seen = 0, sps_offset = 0, pps_offset = 0;
    // 扩展数据的前4位是无用的数据，直接跳过拿到真正的扩展数据
    const uint8_t *extradata = codec_extradata + 4;
    static const uint8_t nalu_header[4] = { 0, 0, 0, 1 }; //sps或者pps数据前面的4bit的特征码

    int length_size = (*extradata++ & 0x3) + 1; // 用于指示表示编码数据长度所需字节数

    sps_offset = pps_offset = -1;

    //extradata第二个字节最后5位用于指示sps的个数,一般情况下一个扩展只有一个sps和pps，之后指针指向下一位
    unit_nb = *extradata++ & 0x1f;

    if (!unit_nb) {
        goto pps;
    }else {
        sps_offset = 0;
        sps_seen = 1;
    }

    while (unit_nb--) {//一般先读SPS在读PPS
        int err;

        // unit_size   = AV_RB16(extradata);
        unit_size   = (extradata[0] << 8) | extradata[1]; //再接着2个字节表示sps或者pps数据的长度
        total_size += unit_size + 4; //4表示sps/pps特征码长度, +=可以统计SPS和PPS的总长度
        if (total_size > INT_MAX - padding) {// total_size太大会造成数据溢出，所以要做判断
            LOGD("Too big extradata size, corrupted stream or invalid MP4/AVCC bitstream");
            av_free(out);
            return AVERROR(EINVAL);
        }

        // extradata + 2 + unit_size比整个扩展数据都长了表明数据是异常的
        if (extradata + 2 + unit_size > codec_extradata + codec_extradata_size) {
            LOGD("Packet header is not contained in global extradata, corrupted stream or invalid MP4/AVCC bitstream");
            av_free(out);
            return AVERROR(EINVAL);
        }

        // av_reallocp()函数用于内存扩展，给out扩展总长加padding的长度
        if ((err = av_reallocp(&out, total_size + padding)) < 0)
            return err;

        // 先将4字节的特征码拷贝进out
        memcpy(out + total_size - unit_size - 4, nalu_header, 4);

        // 再将sps/pps数据拷贝进out,extradata + 2是因为那2字节是表示sps/pps长度的，所以要跳过
        memcpy(out + total_size - unit_size, extradata + 2, unit_size);

        // 本次sps/pps数据处理完后，指针extradata跳过本次sps/pps数据
        extradata += 2 + unit_size;
        pps:
        if (!unit_nb && !sps_done++) { // 执行到这里表明sps已经处理完了，接下来处理pps数据
            unit_nb = *extradata++; /* number of pps unit(s) */
            if (unit_nb) {
                pps_offset = total_size;
                pps_seen = 1;// 如果pps个数大于0这给pps_seen赋值1表明数据中有pps
            }
        }
    }

    if (out) {
        memset(out + total_size, 0, padding);
    }

    if (!sps_seen) {
        LOGD("Warning: SPS NALU missing or invalid. The resulting stream may not play.");
    }

    if (!pps_seen) {
        LOGD("Warning: PPS NALU missing or invalid. The resulting stream may not play.");
    }

    out_extradata->data      = out;
    out_extradata->size      = total_size;

    return length_size;
}

/*
    为包数据添加起始码、SPS/PPS等信息后写入文件。
    AVPacket数据包可能包含一帧或几帧数据，对于视频来说只有1帧，对音频来说就包含几帧
    in为要处理的数据包
    file为输出文件的指针
*/

int h264_mp4toannexb(AVFormatContext *fmt_ctx, AVPacket *in, FILE *dst_fd)
{

    AVPacket *out = NULL;
    AVPacket spspps_pkt;

    int len;
    uint8_t unit_type;
    int32_t nal_size;
    uint32_t cumul_size    = 0;
    const uint8_t *buf;
    const uint8_t *buf_end;
    int buf_size;
    int ret = 0, i;

    out = av_packet_alloc();

    buf      = in->data;
    buf_size = in->size;
    buf_end  = in->data + in->size;

    do {
        ret= AVERROR(EINVAL);
        if (buf + 4 /*s->length_size*/ > buf_end)
            goto fail;

        for (nal_size = 0, i = 0; i < 4/*s->length_size*/; i++)
            nal_size = (nal_size << 8) | buf[i];
        buf += 4; /*s->length_size;*/
        unit_type = *buf & 0x1f;

        if (nal_size > buf_end - buf || nal_size < 0)
            goto fail;

        /*
        if (unit_type == 7)
            s->idr_sps_seen = s->new_idr = 1;
        else if (unit_type == 8) {
            s->idr_pps_seen = s->new_idr = 1;
            */
        /* if SPS has not been seen yet, prepend the AVCC one to PPS */
        /*
        if (!s->idr_sps_seen) {
            if (s->sps_offset == -1)
                av_log(ctx, AV_LOG_WARNING, "SPS not present in the stream, nor in AVCC, stream may be unreadable\n");
            else {
                if ((ret = alloc_and_copy(out,
                                     ctx->par_out->extradata + s->sps_offset,
                                     s->pps_offset != -1 ? s->pps_offset : ctx->par_out->extradata_size - s->sps_offset,
                                     buf, nal_size)) < 0)
                    goto fail;
                s->idr_sps_seen = 1;
                goto next_nal;
            }
        }
    }
    */

        /* if this is a new IDR picture following an IDR picture, reset the idr flag.
         * Just check first_mb_in_slice to be 0 as this is the simplest solution.
         * This could be checking idr_pic_id instead, but would complexify the parsing. */
        /*
        if (!s->new_idr && unit_type == 5 && (buf[1] & 0x80))
            s->new_idr = 1;

        */
        /* prepend only to the first type 5 NAL unit of an IDR picture, if no sps/pps are already present */
        if (/*s->new_idr && */unit_type == 5 /*&& !s->idr_sps_seen && !s->idr_pps_seen*/) {

            h264_extradata_to_annexb( fmt_ctx->streams[in->stream_index]->codec->extradata,
                                      fmt_ctx->streams[in->stream_index]->codec->extradata_size,
                                      &spspps_pkt,
                                      AV_INPUT_BUFFER_PADDING_SIZE);

            if ((ret = alloc_and_copy(out,
                                      spspps_pkt.data, spspps_pkt.size,
                                      buf, nal_size)) < 0)
                goto fail;
            /*s->new_idr = 0;*/
            /* if only SPS has been seen, also insert PPS */
        }
            /*else if (s->new_idr && unit_type == 5 && s->idr_sps_seen && !s->idr_pps_seen) {
                if (s->pps_offset == -1) {
                    av_log(ctx, AV_LOG_WARNING, "PPS not present in the stream, nor in AVCC, stream may be unreadable\n");
                    if ((ret = alloc_and_copy(out, NULL, 0, buf, nal_size)) < 0)
                        goto fail;
                } else if ((ret = alloc_and_copy(out,
                                            ctx->par_out->extradata + s->pps_offset, ctx->par_out->extradata_size - s->pps_offset,
                                            buf, nal_size)) < 0)
                    goto fail;
            }*/ else {
            if ((ret = alloc_and_copy(out, NULL, 0, buf, nal_size)) < 0)
                goto fail;
            /*
            if (!s->new_idr && unit_type == 1) {
                s->new_idr = 1;
                s->idr_sps_seen = 0;
                s->idr_pps_seen = 0;
            }
            */
        }

        //SPS、PPS和特征码都添加后将其写入文件
        len = fwrite(out->data, 1, out->size, dst_fd);
        if(len != out->size){
            LOGD("warning, length of writed data isn't equal pkt.size(%d, %d)", len, out->size);
        }

        // fwrite()只是将数据写入缓存，fflush()才将数据正在写入文件
        fflush(dst_fd);

        next_nal:
        buf += nal_size; // 一帧处理完后将指针移到下一帧
        cumul_size += nal_size + 4;// 累计已经处理好的数据长度
    } while (cumul_size < buf_size);

    /*
    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

    */
    fail:
    av_packet_free(&out); // 凡是中途处理失败退出之前都要将促使的out释放，否则会出现内存泄露

    return ret;
}