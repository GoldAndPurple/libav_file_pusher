#include <fstream>
#include "libav_file_pusher.h"

int main(int argc, char *argv[])
{
    if (argc != 3) {
        printf("usage: %s input_video output_config\n"
               "stream input_video to every URL listed in output_config file\n"
               , argv[0]);
        return -1;
    }

    std::ifstream config_file(argv[2],std::ios::in);
    libavPusher pusher(argv[1],config_file);

    pusher.Start();
    
    config_file.close();
    return 0;
}