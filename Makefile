CFLAGS=-pedantic -errors -Wall

all: BibakBOXClient BibakBOXServer

BibakBOXClient : BibakBOXClient.o
	gcc -o BibakBOXClient BibakBOXClient.o -lpthread

BibakBOXServer : BibakBOXServer.o
	gcc -o BibakBOXServer BibakBOXServer.o -lpthread

clean : 
	rm *.o BibakBOXClient BibakBOXServer
