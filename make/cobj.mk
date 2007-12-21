%.o: $(S)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# used by rgmanager/src/daemons
%-noccs.o: $(S)/%.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

# used by fence/agents/xvm
%-standalone.o: $(S)/%.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<
