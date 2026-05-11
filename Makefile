.PHONY: all clean

MYSQL_CFLAGS := $(shell mysql_config --cflags 2>/dev/null)
MYSQL_LIBS := $(shell mysql_config --libs 2>/dev/null)

all: clean main

main:
	g++ main.cpp \
	config.cpp \
	db/MySQLStore.cpp \
	db/PasswordHash.cpp \
	http/Connection.cpp \
	reactor/Eventloop.cpp \
	reactor/Epoller.cpp \
	logs/Logger.cpp \
	utils/Buffer.cpp \
	-I. $(MYSQL_CFLAGS) -std=c++17 -O2 -DNDEBUG -pthread -flto \
	$(MYSQL_LIBS) \
	-o main

clean:
	rm -f main
