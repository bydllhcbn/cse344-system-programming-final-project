
### CSE 344 System Programming, Final Project

In this project you are expected to implement a “simpler” version of Dropbox.

The server side is expected to backup the files of the client by mirroring. While the client is connected to the
server, modifications done to client’s directory (add, delete or modify file) must also be done in the server
side. Therefore, the two directories must be consistent while the connection is active. The server must be
able to handle multiple clients at the same time (a multi-threaded internet server). Your server should also
log the create, delete and update operations of all files in a log file under the corresponding directory
reserved for the client. We will not touch to any file in the server side while testing.

An example call for the server should be of the form

> BibakBOXServer [directory] [threadPoolSize] [portnumber]
 
where the directory is the servers specific area for file operations (there shouldn’t be multiple servers
running on the same directory in the same computer), threadPoolSize is the maximum number of
threads active at a time (meaning maximum number of active connected clients), portnumber is the port
server will wait for connection.
An example call from the client might be of the form

> BibakBOXClient [dirName] [portnumber]

where dirName is the name of the directory on server side and portnumber is the connection port of
the server. The path of the directory identifies each client. When a client that was connected before connects
again, it will receive missing files from the server if there is any. The files that were created or modified
while the client is offline should be detected and copied to the server side after connecting.

 Note that the client should return with a proper message when server is down and server should prompt a
message when a client connection is accepted (with the address of connection) to the screen .

Test your code with multiple (10, 20, 50 ) clients, reconnect to see if the server updates the client
information properly. Check what happens when a new file is added, edited or removed on the client size
when the client server connection is still active. Write a report examining at least 5 different cases
