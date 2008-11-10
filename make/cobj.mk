%.o: $(S)/%.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(REENT_CFLAGS) -c -o $@ $<

# used by dlm/libdlm
%_lt.o: $(S)/%.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

# used by rgmanager/src/daemons
%-noccs.o: $(S)/%.c
	$(CC) $(CFLAGS) $(NOCCS_CFLAGS) -c -o $@ $<

# used by fence/agents/xvm
%-standalone.o: $(S)/%.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(STANDALONE_CFLAGS) -c -o $@ $<
