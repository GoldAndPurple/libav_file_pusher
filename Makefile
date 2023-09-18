
all:
	  g++ -Wall -g -std=c++17 main.cpp -lavcodec -lavformat -lavfilter -lavdevice -lswresample -lswscale -lavutil -pthread -o test