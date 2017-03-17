CC = g++
all: server client
server: sender.cpp
	$(CC) sender.cpp -o sender -w
client: receiver.cpp
	$(CC) receiver.cpp -o receiver -w
clean:
	rm -f sender receiver received.data
