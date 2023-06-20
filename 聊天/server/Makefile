CFLAGS+=-pthread  

all:epoll_server

epoll_server:epoll_server.o 
	$(CC) $^ -o $@ $(CFLAGS)


clean:
	rm *.o epoll_server -rf
