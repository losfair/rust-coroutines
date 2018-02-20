cflags.common := -g -fPIC -O2 -Wall -pthread
cxxflags.common := -g -fPIC -O2 -Wall -pthread
cflags.cc := -fgnu-tm -DHAS_TM
cflags.gcc := -fgnu-tm -DHAS_TM
cxxflags.g++ := -fgnu-tm -DHAS_TM

CFLAGS := ${cflags.common} ${cflags.${CC}}
CXXFLAGS := ${cxxflags.common} ${cxxflags.${CXX}}

all: scheduler scheduler_test unblock_hook unblock_hook_test

scheduler: scheduler.o sched_helper.o
	$(CC) $(CFLAGS) -shared -o libscheduler.so scheduler.o sched_helper.o

scheduler_test: scheduler scheduler_test.o
	$(CXX) $(CXXFLAGS) -o scheduler_test scheduler_test.o scheduler.o sched_helper.o

unblock_hook: scheduler unblock_hook.o
	$(CC) $(CFLAGS) -shared -o libunblock_hook.so unblock_hook.o scheduler.o sched_helper.o

unblock_hook_test: unblock_hook unblock_hook_test.o
	$(CXX) $(CXXFLAGS) -o unblock_hook_test unblock_hook_test.o scheduler.o sched_helper.o unblock_hook.o -ldl

clean:
	rm *.o *.so scheduler_test
