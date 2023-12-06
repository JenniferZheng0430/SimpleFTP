all: server1 client1
client1: client1.o
	gcc -o client1 client1.o
client1.o: client1.c
	gcc -c client1.c

# set the rule of time_server
server1: server1.o
	gcc -o server1 server1.o
server1.o: server1.c
	gcc -c server1.c
# clean up the generated files and the executable file
clean:
	rm -f *.o client1 server1