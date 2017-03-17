CC = g++
all: server client
server: server.cpp
	$(CC) server.cpp -o server -w
client: client.cpp
	$(CC) client.cpp -o client -w
clean:
	rm -f server client received.data
