#include <stdlib.h>

#include "list.h"
#include "buffer.h"

int main(int argc, char *argv[])
{
	struct buffer *buffer = new_buffer(0x64, 4096);
	show_dirty_buffers();
	set_buffer_dirty(buffer);
	show_dirty_buffers();
	set_buffer_uptodate(buffer);
	show_dirty_buffers();
	return 0;
}
