CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE
CLIENT = ftp_client
SERVER = ftp_server
CLIENT_SRC = ftp_client.c
SERVER_SRC = ftp_server.c

all: $(CLIENT) $(SERVER)

$(CLIENT): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC)

$(SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC)

client: $(CLIENT)

server: $(SERVER)

clean:
	rm -f $(CLIENT) $(SERVER)
	rm -rf ftp_root

install: $(CLIENT) $(SERVER)
	sudo cp $(CLIENT) /usr/local/bin/
	sudo cp $(SERVER) /usr/local/bin/

uninstall:
	sudo rm -f /usr/local/bin/$(CLIENT)
	sudo rm -f /usr/local/bin/$(SERVER)

test: $(SERVER) $(CLIENT)
	@echo "Starting test setup..."
	@echo "1. Start the server: ./$(SERVER)"
	@echo "2. In another terminal, start the client: ./$(CLIENT)"
	@echo "3. Use these commands in the client:"
	@echo "   connect localhost 2121"
	@echo "   login test anypassword"
	@echo "   list"

.PHONY: all client server clean install uninstall test