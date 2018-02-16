CC := clang
CXX := clang++
CFLAGS := -g -fPIC -O3 -Wall
CXXFLAGS := -g -fPIC -O3 -Wall

all: scheduler scheduler_test

scheduler: scheduler.o sched_helper.o
	$(CC) $(CFLAGS) -shared -o libscheduler.so scheduler.o sched_helper.o

scheduler_test: scheduler scheduler_test.o
	$(CXX) -o scheduler_test scheduler_test.o scheduler.o sched_helper.o -lpthread

clean:
	rm *.o scheduler_test
