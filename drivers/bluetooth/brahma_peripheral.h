/* SPDX-License-Identifier: GPL-2.0-only */
/* Brahma 2.0 peripheral bootstrap, captured on OPD2413 OEM Android.
 * Kept independent of the UART API so the real protocol can be replayed.
 */
#ifndef OPD2413_BRAHMA_PERIPHERAL_H
#define OPD2413_BRAHMA_PERIPHERAL_H

struct brahma_image {
	const unsigned char *data;
	size_t size;
};

struct brahma_io {
	void *context;
	int (*send)(void *, const unsigned char *, size_t);
	int (*receive)(void *, unsigned char *, size_t);
	int (*baud)(void *);
	void (*flow)(void *, bool);
	void (*stage)(void *, const char *);
};

static int brahma_receive(struct brahma_io *io, unsigned char *data)
{
	int n = io->receive(io->context, data, 260);

	if (n < 0)
		return n;
	if (n < 4 || n > 259 || data[0] != 0x34 || data[1] ||
	    data[2] != 0xff || data[3] != n - 4)
		return -EPROTO;
	return n;
}

static int brahma_complete(struct brahma_io *io, unsigned int opcode,
			   unsigned char command, unsigned char status,
			   unsigned char *response)
{
	int n = brahma_receive(io, response);

	if (n < 0)
		return n;
	if (n < 11 || response[4] != 0xf0 || response[5] != 1 ||
	    response[6] != 1 || response[7] != (opcode & 0xff) ||
	    response[8] != (opcode >> 8) || response[9] != status ||
	    response[10] != command)
		return -EPROTO;
	return n;
}

static int brahma_command(struct brahma_io *io, unsigned int opcode,
			  const unsigned char *params, size_t count,
			  unsigned char *response)
{
	unsigned char packet[260];
	int ret;

	if (!count || count > 255)
		return -EINVAL;
	packet[0] = 0x31;
	packet[1] = 0;
	packet[2] = opcode;
	packet[3] = opcode >> 8;
	packet[4] = count;
	memcpy(packet + 5, params, count);
	ret = io->send(io->context, packet, count + 5);
	if (ret)
		return ret;
	return brahma_complete(io, opcode, params[0], 0, response);
}

static int brahma_wake(struct brahma_io *io)
{
	unsigned char value = 0xfd, response[260];
	int ret = io->send(io->context, &value, 1);

	if (ret)
		return ret;
	ret = io->receive(io->context, response, sizeof(response));
	return ret < 0 ? ret : (ret == 1 && response[0] == 0xfc ? 0 : -EPROTO);
}

static int brahma_version(struct brahma_io *io, unsigned char subsystem)
{
	unsigned char params[] = {5, subsystem}, response[260];
	int n = brahma_command(io, 0xfff0, params, sizeof(params), response);

	if (n < 0)
		return n;
	if (n != 25 || response[11] != subsystem ||
	    memcmp(response + 12, "\x21\0\0\0", 4) ||
	    memcmp(response + 18, "\0\x02\0\x02\x21\x40", 6))
		return -ENODEV;
	return 0;
}

static int brahma_download(struct brahma_io *io, unsigned char subsystem,
			   const struct brahma_image *image, bool nvm)
{
	unsigned char packet[251], response[260];
	size_t offset = 0, count;
	int ret;

	if (!image->data || image->size < 4 || image->size > 1024 * 1024)
		return -EINVAL;
	if (nvm) {
		unsigned int length = image->data[1] | image->data[2] << 8 |
				      image->data[3] << 16;
		if (image->data[0] != 2 || length != image->size - 4)
			return -EINVAL;
	} else if (image->size < 52 || memcmp(image->data, "\177ELF\1\1\1", 7)) {
		return -EINVAL;
	}
	while (offset < image->size) {
		count = image->size - offset;
		if (count > 243)
			count = 243;
		packet[0] = 0x31;
		packet[1] = 0;
		packet[2] = 0xf0;
		packet[3] = 0xff;
		packet[4] = count + 3;
		packet[5] = 6;
		packet[6] = subsystem;
		packet[7] = count;
		memcpy(packet + 8, image->data + offset, count);
		ret = io->send(io->context, packet, count + 8);
		if (ret)
			return ret;
		offset += count;
		if (nvm || offset == image->size) {
			ret = brahma_complete(io, 0xfff0, 6, 0, response);
			if (ret < 0)
				return ret;
			if (ret != 12 || response[11] != subsystem)
				return -EPROTO;
		}
	}
	return 0;
}

static int brahma_notification(struct brahma_io *io, unsigned char kind,
			       unsigned char a, unsigned char b)
{
	unsigned char response[260];
	int n = brahma_receive(io, response);

	if (n < 0)
		return n;
	return n == 8 && response[4] == 0xf0 && response[5] == kind &&
	       response[6] == a && response[7] == b ? 0 : -EPROTO;
}

static int brahma_arbitrate(struct brahma_io *io, unsigned char subsystem)
{
	unsigned char params[] = {8, subsystem, 0}, response[260];
	int n = brahma_command(io, 0xfff0, params, sizeof(params), response);

	if (n < 0)
		return n;
	if (n != 12 || response[11] != subsystem)
		return -EPROTO;
	return 0;
}

static int brahma_peripheral_boot(struct brahma_io *io,
				 const struct brahma_image *patch,
				 const struct brahma_image *nvm,
				 const struct brahma_image *tmel)
{
	unsigned char response[260], baud[] = {0x31, 0, 0xf1, 0xff, 2, 2, 0x15};
	unsigned char board[] = {10}, reset[] = {3}, activate[] = {0, 1, 1};
	int ret;

	io->stage(io->context, "peripheral-version-115200");
	ret = brahma_wake(io);
	if (ret)
		return ret;
	ret = brahma_version(io, 0);
	if (ret)
		return ret;
	io->stage(io->context, "peripheral-baud-8000000");
	io->flow(io->context, true);
	ret = io->send(io->context, baud, sizeof(baud));
	if (!ret)
		ret = io->baud(io->context);
	io->flow(io->context, false);
	if (ret)
		return ret;
	ret = brahma_complete(io, 0xfff1, 2, 1, response);
	if (ret < 0)
		return ret;
	io->stage(io->context, "peripheral-patch");
	ret = brahma_arbitrate(io, 0);
	if (ret)
		return ret;
	ret = brahma_download(io, 0, patch, false);
	if (ret)
		return ret;
	ret = brahma_command(io, 0xfff0, board, sizeof(board), response);
	if (ret < 0)
		return ret;
	/* The measured board is 0000 and OEM selects its default NVM. */
	if (ret != 14 || response[11] != 2 || response[12] || response[13])
		return -ENODEV;
	ret = brahma_download(io, 0, nvm, true);
	if (ret)
		return ret;
	ret = brahma_command(io, 0xfff1, reset, sizeof(reset), response);
	if (ret < 0)
		return ret;
	ret = brahma_notification(io, 3, 0, 0);
	if (ret)
		return ret;
	io->stage(io->context, "tmel-patch");
	ret = brahma_wake(io);
	if (ret)
		return ret;
	ret = brahma_arbitrate(io, 3);
	if (ret)
		return ret;
	ret = brahma_version(io, 3);
	if (ret)
		return ret;
	ret = brahma_download(io, 3, tmel, false);
	if (ret)
		return ret;
	ret = brahma_notification(io, 3, 0, 3);
	if (ret)
		return ret;
	io->stage(io->context, "activate-bluetooth");
	/* Activate has a status response followed by an async completion. */
	{
		unsigned char packet[] = {0x31, 0, 0xf1, 0xff, 3, 0, 1, 1};
		ret = io->send(io->context, packet, sizeof(packet));
	}
	if (ret)
		return ret;
	ret = brahma_receive(io, response);
	if (ret < 0)
		return ret;
	if (ret != 10 || memcmp(response + 4, "\xf0\0\0\1\xf1\xff", 6))
		return -EPROTO;
	return brahma_notification(io, 2, activate[1], activate[2]);
}
#endif
