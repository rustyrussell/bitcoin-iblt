#include <stdio.h>
#include <ccan/rbuf/rbuf.h>
#include <ccan/err/err.h>
#include <ccan/read_write_all/read_write_all.h>
#include <ccan/short_types/short_types.h>
#include <ccan/str/hex/hex.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
	struct rbuf rb;
	char *line;
	char filename[sizeof("../txcache/0123456789012345678901234567890123456789012345678901234567890123")] = "../txcache/";
	size_t num = 0;

	err_set_progname(argv[0]);
	rbuf_init(&rb, STDIN_FILENO, malloc(1024), 1024);

	while ((line = rbuf_read_str(&rb, '\n', realloc)) != NULL) {
		int fd;
		u64 fee;
		char *endp, *space = strchr(line, ' ');
		if (!space || space != line + 64)
			errx(1, "Expected ' ' in input");
		memcpy(filename + strlen("../txcache/"), line, space - line);
		fd = open(filename, O_WRONLY|O_TRUNC|O_CREAT, 0600);
		if (fd < 0)
			err(1, "Opening %s", filename);
		// 8 bytes for fee (native end), rest is tx.
		fee = strtoull(space + 1, &endp, 10);
		space = strchr(space + 1, ' ');
		if (!space || endp != space)
			errx(1, "Bad input format in '%s'", line);
		if (!write_all(fd, &fee, sizeof(fee)))
			err(1, "Writing fee to %s", filename);
		else {
			u8 txstr[hex_data_size(strlen(space+1))];
			if (!hex_decode(space + 1, strlen(space+1),
					&txstr, sizeof(txstr)))
				errx(1, "Bad hex string %s", space+1);
			if (!write_all(fd, txstr, sizeof(txstr)))
				err(1, "Writing transaction");
		}
		close(fd);
		num++;
	}
	printf("%zu txcache entries created\n", num);
	return 0;
}

	
