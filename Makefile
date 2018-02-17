CC := clang
CXX := clang++
CFLAGS := -g -fPIC -O3 -Wall -pthread
CXXFLAGS := -g -fPIC -O3 -Wall -pthread

all: scheduler scheduler_test unblock_hook unblock_hook_test

scheduler: scheduler.o sched_helper.o
	$(CC) $(CFLAGS) -shared -o libscheduler.so scheduler.o sched_helper.o

scheduler_test: scheduler scheduler_test.o
	$(CXX) $(CXXFLAGS) -o scheduler_test scheduler_test.o scheduler.o sched_helper.o

unblock_hook: unblock_hook.o
	$(CC) $(CFLAGS) -shared -o unblock_hook.so unblock_hook.o scheduler.o sched_helper.o

unblock_hook_test: unblock_hook unblock_hook_test.o
	$(CXX) $(CXXFLAGS) -o unblock_hook_test unblock_hook_test.o scheduler.o sched_helper.o unblock_hook.o -ldl

clean:
	rm *.o scheduler_test
