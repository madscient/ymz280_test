#pragma once

typedef signed int stream_sample_t;

class virtual_sound_chip_t
{
public:
	virtual_sound_chip_t() {};
	virtual void register_write(uint32_t address, uint32_t data) = 0;
	virtual uint32_t register_read(uint32_t address) = 0;
	virtual void generate_sample(stream_sample_t **outputs, int samples) = 0;
	virtual void device_reset() = 0;
	virtual void device_start() = 0;
};

uint8_t read_byte(uint32_t address);
void write_byte(uint32_t address, uint8_t data);
void init_emu();