CC=gcc
CFLAGS=-Wall -Wextra -O3 -pthread
all: log_analyzer_seq log_analyzer_par log_analyzer_par_optimized
log_analyzer_seq: log_analyzer_seq.c log_common.h
	$(CC) $(CFLAGS) $< -o $@
log_analyzer_par: log_analyzer_par.c log_parser.c parallel_util.c log_common.h log_parser.h parallel_util.h
	$(CC) $(CFLAGS) log_analyzer_par.c log_parser.c parallel_util.c -o $@
log_analyzer_par_optimized: log_analyzer_par_optimized.c log_parser.c parallel_util.c log_common.h log_parser.h parallel_util.h
	$(CC) $(CFLAGS) log_analyzer_par_optimized.c log_parser.c parallel_util.c -o $@
clean:
	rm -f log_analyzer_seq log_analyzer_par log_analyzer_par_optimized *.o
