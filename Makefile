#Notes: 
#How to use it:
#Build the application: Open your terminal and run:
#Bash command, 
#make
#This uses the command specified in your source file: 
#gcc -o scrible scrible.c pkg-config --cflags --libs gtk+-3.0 gtksourceview-3.0``.
#Install the application: 
#To move the binary to your system's executable path (defaulting to /usr/local/bin):
#Bash command, 
#sudo make install
#Clean up: To remove the compiled binary from your current folder:
#Bash command, 
#make clean
#Prerequisites
#Before running make, ensure you have the required development libraries installed as noted in the source header:
#Bash command, 
#sudo apt-get install libgtk-3-dev libgtksourceview-3.0-dev

# Compiler and Flags
CC = gcc
# Fetch flags from scrible.c comments
CFLAGS = `pkg-config --cflags gtk+-3.0 gtksourceview-3.0` -Wall -Wextra -g
LIBS = `pkg-config --libs gtk+-3.0 gtksourceview-3.0`

# Target names
TARGET = scrible
SRC = scrible.c
PREFIX = /usr/local

# Default build rule
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) -o $(TARGET) $(SRC) $(CFLAGS) $(LIBS)

# Install rule (requires sudo)
install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin

# Uninstall rule
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin($(TARGET))

# Clean build files
clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
