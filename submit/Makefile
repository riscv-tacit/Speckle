RV_TOOLCHAIN = ${RISCV}
CC = ${RV_TOOLCHAIN}/bin/riscv64-unknown-linux-gnu-gcc
DUMP = ${RV_TOOLCHAIN}/bin/riscv64-unknown-linux-gnu-objdump
CFLAGS := -O3 -static -DRISCV

#CC = gcc
#CFLAGS := -O3 -std=gnu99

trace-submit: trace-submit.c tacit.h
	${CC} ${CFLAGS} -o trace-submit trace-submit.c

trace-start: trace-start.c tacit.h
	${CC} ${CFLAGS} -o trace-start trace-start.c

trace-stop: trace-stop.c tacit.h
	${CC} ${CFLAGS} -o trace-stop trace-stop.c

time-submit: time-submit.c
	${CC} ${CFLAGS} -o time-submit time-submit.c

dump: trace-submit
	${DUMP} -D trace-submit > trace-submit.dump

clean:
	rm -f trace-submit trace-submit.dump trace-start trace-stop trace-stop.dump time-submit
