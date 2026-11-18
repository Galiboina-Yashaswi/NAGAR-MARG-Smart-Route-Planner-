CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude -O2
LDFLAGS = -lm

COMMON_SRCS = src/graph.c src/minheap.c src/dijkstra.c src/navigation.c
COMMON_OBJS = $(COMMON_SRCS:.c=.o)

TARGET_PLANNER = route_planner.exe
TARGET_CLI = route_cli.exe
TARGET_TEST = test_route.exe

all: $(TARGET_PLANNER) $(TARGET_CLI) $(TARGET_TEST)

$(TARGET_PLANNER): $(COMMON_OBJS) main.o
	$(CC) $(CFLAGS) -o $(TARGET_PLANNER) $(COMMON_OBJS) main.o $(LDFLAGS)

$(TARGET_CLI): $(COMMON_OBJS) src/route_cli.o
	$(CC) $(CFLAGS) -o $(TARGET_CLI) $(COMMON_OBJS) src/route_cli.o $(LDFLAGS)

$(TARGET_TEST): $(COMMON_OBJS) test_route.o
	$(CC) $(CFLAGS) -o $(TARGET_TEST) $(COMMON_OBJS) test_route.o $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET_TEST)
	./$(TARGET_TEST)

clean:
	rm -f src/*.o *.o $(TARGET_PLANNER) $(TARGET_CLI) $(TARGET_TEST)

run: $(TARGET_PLANNER)
	./$(TARGET_PLANNER)

run_cli: $(TARGET_CLI)
	./$(TARGET_CLI) --source 0 --destination 5 --mode time --map map_data.json

.PHONY: all clean run run_cli test
