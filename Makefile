FLAGS=-O2 -mtune=native -march=native
SRCS=
TARGET=wlauncher

all: $(TARGET)

wlauncher: $(SRCS) src/main.c
	gcc $(FLAGS) $(SRCS) -o $(TARGET) src/main.c

run:
	./wlauncher

clean:
	rm -f *.d *.o $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/bin/$(TARGET)
	chmod go+rx /usr/bin/$(TARGET)
