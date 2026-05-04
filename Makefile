.PHONY: all clean

all: clean main

main:
	g++ main.cpp \
	http/Connection.cpp \
	reactor/Eventloop.cpp \
	reactor/Epoller.cpp \
	utils/Logger.cpp \
	utils/Buffer.cpp \
	-I. -std=c++17 -O2 -DNDEBUG -pthread -flto \
	-o main

clean:
	rm -f main