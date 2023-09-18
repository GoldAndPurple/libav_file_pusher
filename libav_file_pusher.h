#pragma once

#include <vector>
#include <deque>
#include <iostream>
#include <string>
#include <thread>

#ifdef __cplusplus 
extern "C" {

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/time.h"

}
#endif

class libavPusher {

    class pushProcessor {
        AVFormatContext* ifmt_ctx = nullptr;
        AVFormatContext* ofmt_ctx = nullptr;
        std::deque<AVPacket*> packets;
        bool process_active = false;
        bool shutting_down = false;

        bool string_starts_with(const std::string& s, const std::string& prefix){
            return (s.compare(0, prefix.size(), prefix) == 0);
        }

    public:
        int QueuePacket(AVPacket *pkt) {
            AVPacket *pkt1 = av_packet_alloc();
            av_packet_ref(pkt1, pkt);
            packets.push_back(pkt1);
            return 0;
        }

        void Process(){
        process_active = true;
            while (process_active){
                if (shutting_down){
                    av_write_trailer(ofmt_ctx);
                    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                        avio_closep(&ofmt_ctx->pb);
                    }
                    // Set exit flag
                    process_active = false;
                    avformat_free_context(ofmt_ctx);
                } else {
                    int ret = 0;
                    while(packets.size() > 0) {
                        AVPacket *pkt = packets.front();
                        packets.pop_front();

                        AVStream *in_stream, *out_stream;
                        in_stream = ifmt_ctx->streams[pkt->stream_index];
                        out_stream = ofmt_ctx->streams[pkt->stream_index];
                        pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                        pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                        pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
                        pkt->pos = -1;
                        ret = av_interleaved_write_frame(ofmt_ctx, pkt);

                        av_packet_free(&pkt);
                        if (ret != 0) {
                            std::cout << "av_interleaved_write_frame err" << ret << std::endl;
                            shutting_down = true;
                            break;
                        }
                    }
                }
            }
        std::cout << "output thread exit!" << std::endl;
        }

        int Finish() {
            shutting_down = true;
            return 0;
        }

        pushProcessor(std::string output_url, AVFormatContext *input_context){
            int ret = 0;
            const char* format_name = NULL;
            ifmt_ctx = input_context;
            if (string_starts_with(output_url, "rtsp://")) {
                format_name = "rtsp";
            }else if(string_starts_with(output_url, "udp://") || string_starts_with(output_url, "tcp://")) {
                format_name = "h264";
            }else if(string_starts_with(output_url, "rtp://")) {
                format_name = "rtp";
            }else if(string_starts_with(output_url, "rtmp://")) {
                format_name = "flv";
            }
            else{
                std::cout << "Not support this Url:" << output_url << std::endl;
                return;
            }

            if (nullptr == ofmt_ctx) {
                ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, format_name, output_url.c_str());
                if (ret < 0 || ofmt_ctx == NULL) {
                    std::cout << "avformat_alloc_output_context2() err=" << ret << std::endl;
                    return;
                }

                for(uint i = 0; i < ifmt_ctx->nb_streams; ++i) {
                    AVStream *ostream = avformat_new_stream(ofmt_ctx, NULL);
                    if (NULL == ostream) {
                        std::cout << "Can't create new stream!" << std::endl;
                        return;
                    }
                    ret = avcodec_parameters_copy(ostream->codecpar, ifmt_ctx->streams[i]->codecpar);
                    if (ret < 0) {
                        std::cout << "avcodec_parameters_copy() err=" << ret << std::endl;
                        return;
                    }
                    ostream->codecpar->codec_tag = 0;
                }
            }

            if (!ofmt_ctx) {
                printf("Could not create output context\n");
                return;
            }

            av_dump_format(ofmt_ctx, 0, output_url.c_str(), 1);

            if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                ret = avio_open(&ofmt_ctx->pb, output_url.c_str(), AVIO_FLAG_WRITE);
                if (ret < 0) {
                    printf("Could not open output URL '%s'", output_url.c_str());
                    return;
                }
            }

            AVDictionary *opts = NULL;
            if (string_starts_with(output_url, "rtsp://")) {
                av_dict_set(&opts, "rtsp_transport", "tcp", 0);
                av_dict_set(&opts, "muxdelay", "0.1", 0);
            } 
            // else if (string_starts_with(output_url, "rtmp://")){
            //     ofmt_ctx->flags = FLV_NO_DURATION_FILESIZE;
            // }

            ret = avformat_write_header(ofmt_ctx, &opts);
            if (ret < 0) {
                printf("Error occurred when opening output URL\n");
                return;
            }

            return;
        }
    };

    AVFormatContext* ifmt_ctx = nullptr;
    std::vector<pushProcessor*> pushers;
    std::vector<std::thread*> workers;
    int frame_index = -1;
    int video_stream_idx = -1;
    int64_t startTime = 0;

public: 
    
    libavPusher(const std::string& input_filename, std::istream& output_url_list){
        // avformat_network_init();
        int ret = 0;

        if ((ret = avformat_open_input(&ifmt_ctx, input_filename.c_str(), 0, 0)) < 0) {
            printf("Could not open input file.");
            return;
        }
        if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
            printf("Failed to retrieve input stream information");
            return;
        }
        for(uint i = 0; i < ifmt_ctx->nb_streams; ++i) {
            if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                video_stream_idx = i;
                break;
            }
        }
        av_dump_format(ifmt_ctx, 0, input_filename.c_str(), 0);

        if (output_url_list) {
            std::string line;
            while (std::getline(output_url_list, line)) {
                pushers.push_back( new pushProcessor(line,ifmt_ctx) );
                // ret = pushers.back()->OpenOutputStream(line, ifmt_ctx);
                // if (ret != 0){
                //     pushers.pop_back();
                // }
            }
        }
    }

    ~libavPusher(){
        for (uint i = 0; i < workers.size(); i++){
            pushers[i]->Finish();
            workers[i]->join();

            delete workers[i];
            delete pushers[i];
        }

        avformat_close_input(&ifmt_ctx);
        avformat_free_context(ifmt_ctx);
    }

    void Start(){
        for (auto const& pusher : pushers){
            workers.push_back(new std::thread(&pushProcessor::Process, pusher));
        }
        AVPacket pkt;
        while (true) {
            if (av_read_frame(ifmt_ctx, &pkt) < 0) {
                break;
            };

            if (pkt.pts == AV_NOPTS_VALUE)
            {
                //Write PTS
                AVRational time_base1 = ifmt_ctx->streams[video_stream_idx]->time_base;
                //Duration between 2 frames (us)
                int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(ifmt_ctx->streams[video_stream_idx]->r_frame_rate);
                //Parameters
                pkt.pts = (double)(frame_index * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
                pkt.dts = pkt.pts;
                pkt.duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);
            }
            if (pkt.stream_index == video_stream_idx)
            {
                AVRational time_base = ifmt_ctx->streams[video_stream_idx]->time_base;
                AVRational time_base_q = {1, AV_TIME_BASE};
                int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
                int64_t now_time = av_gettime() - startTime;
                if (pts_time > now_time)
                    av_usleep(pts_time - now_time);
                av_usleep(av_rescale_q(pkt.duration, time_base, time_base_q));
            }

            for (auto const& pusher : pushers){
                pusher->QueuePacket(&pkt);
            }
            
            av_packet_unref(&pkt);
            frame_index++;
            // av_usleep(40000);
        }
    }
};
