CC := gcc
GUEST_DIRS := $(wildcard guest*/)
GUEST_SRCS := $(foreach dir,$(GUEST_DIRS),$(wildcard $(dir)guest*.c))
GUEST_OBJS := $(patsubst %.c, %.o, $(GUEST_SRCS))
GUEST_IMGS := $(patsubst %.c, %.img, $(GUEST_SRCS))

all: $(GUEST_IMGS) mini_hypervisor

mini_hypervisor: mini_hypervisor.cpp
	g++ -g $^ -o $@

%.o: %.c
	$(CC) -m64 -ffreestanding -fno-pic -c -o $@ $<

%.img: %.o
	ld -T $(patsubst %.img, %.ld, $@) $< -o $@

clean:
	rm -f mini_hypervisor $(GUEST_OBJS) $(GUEST_IMGS) IO_library.o
	find $(GUEST_DIRS) -name '*.txt' -exec rm -f {} +
	find $(GUEST_DIRS) -name '*.ppm' -exec rm -f {} +

run1:
	./mini_hypervisor -m 4 -p 2 -g guest1/guest1.img guest2/guest2.img -f lorem1.txt lorem2.txt

run2:
	./mini_hypervisor -m 4 -p 2 -g guest3/guest3.img guest4/guest4.img -f lorem1.txt lorem2.txt

run3:
	./mini_hypervisor -m 4 -p 2 -g guest5/guest5.img -f lorem1.txt lorem2.txt