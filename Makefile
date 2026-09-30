CC = gcc
CFLAGS = -Wall -Wextra -O3 -pthread
TARGET_SEQ = log_analyzer_seq
TARGET_PAR = log_analyzer_par
TARGET_OPT = log_analyzer_par_optimized

all: $(TARGET_SEQ)

$(TARGET_SEQ): log_analyzer_seq.c log_common.h
    $(CC) $(CFLAGS) log_analyzer_seq.c -o $(TARGET_SEQ)

par: log_analyzer_par.c log_common.h
    $(CC) $(CFLAGS) log_analyzer_par.c -o $(TARGET_PAR)

opt: log_analyzer_par_optimized.c log_common.h
    $(CC) $(CFLAGS) log_analyzer_par_optimized.c -o $(TARGET_OPT)

clean:
    rm -f $(TARGET_SEQ) $(TARGET_PAR) $(TARGET_OPT) *.o