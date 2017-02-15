EXEC = en
OBJS = en.o clio.o

all: $(EXEC)

$(EXEC): $(OBJS)

clean:
	$(RM) $(EXEC) $(OBJS)

distclean: clean
	$(RM) *~

