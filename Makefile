SRCF := ./src
INCF := ./inc
OUTF := ./out
CC = gcc -I $(INCF)
CFLAGS = -Wall -Wextra -std=c99 
SRCS := $(wildcard $(SRCF)/*.c)
OBJS := $(SRCS:$(SRCF)/%.c=$(OUTF)/%.o)

TARGET = ./server.exe

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(OUTF)/%.o : $(SRCF)/%.c 
	$(CC) $(CFLAGS) -c $< -o $@ 

$(OUTF)/%.d : $(SRCF)/%.c 
	$(CC) -MM $< > $@

include $(SRCS:$(SRCF)/%.c=$(OUTF)/%.d)

.PHONY: valgrind
valgrind : CFLAGS += -g3
valgrind : 
	echo $(CFLAGS)
	make clean
	@echo ''
	make $(TARGET)
	@echo ''
	valgrind -q --leak-check=yes --undef-value-errors=yes --track-origins=yes $(TARGET)

.PHONY: clean
clean :
	$(RM) *.exe out/*

.PHONY: logs_clean
logs_clean :
	$(RM) ./logs/*


# Test curls
.PHONY: head
head:
	curl -I http://localhost:8080/

.PHONY: err_404
err_404:
	curl -I http://localhost:8080/qweqwe
	curl -v http://localhost:8080/qweqwe


.PHONY: err_405
err_405:
	curl -f -X POST http://localhost:8080/text.txt

.PHONY: err_403
err_403:
	curl --path-as-is http://localhost:8080/../text.txt

.PHONY: err_perm
err_perm:
	touch ./static/secret.txt
	chmod 000 ./static/secret.txt
	curl -v http://localhost:8080/secret.txt

.PHONY: err_too_big
err_too_big:
	dd if=/dev/zero of=static/bigfile.txt bs=1M count=130
	curl -f http://localhost:8080/bigfile.txt

