all:
	gcc BibakBOXServer.c -pthread -Wall -pedantic-errors -g -o BibakBOXServer
	gcc BibakBOXClient.c -pthread -Wall -pedantic-errors -g -o BibakBOXClient
