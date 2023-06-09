FLAG=T910=1

all: main.o
	gcc -D${FLAG} -fsanitize=address -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Werror -std=c17 -Wpedantic -O0 -g -pthread main.o a5.o -o a5

main.o: main.c a5.o
	gcc -D${FLAG} -fsanitize=address -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Werror -std=c17 -Wpedantic -O0 -g -pthread -c main.c -o main.o

a5.o: a5.c
	gcc -D${FLAG} -fsanitize=address -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Werror -std=c17 -Wpedantic -O0 -g -pthread -c a5.c -o a5.o

clean:
	rm -rf *.o a5 d
