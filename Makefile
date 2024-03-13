all: server subscriber

server: server.cpp common.cpp
	g++ -o server server.cpp common.cpp

subscriber: subscriber.cpp common.cpp
	g++ -o subscriber subscriber.cpp common.cpp -lm

clean:
	rm -rf server subscriber