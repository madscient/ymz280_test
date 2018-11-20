// license:BSD-3-Clause
// copyright-holders:Aaron Giles
/**********************************************************************************************
 *
 *   Yamaha YMZ280B driver
 *   by Aaron Giles
 *
 **********************************************************************************************/

#ifndef MAME_SOUND_YMZ280B_H
#define MAME_SOUND_YMZ280B_H

#pragma once


#define YMZ280B_MAKE_WAVS 0

#define MCFG_YMZ280B_IRQ_HANDLER(_devcb) \
	downcast<ymz280b_device &>(*device).set_irq_handler(DEVCB_##_devcb);

class ymz280b_device : public virtual_sound_chip_t
{
public:
	ymz280b_device(uint32_t clock);

	virtual void register_write(uint32_t adress, uint32_t data);
	virtual uint32_t register_read(uint32_t address);
	virtual void generate_sample(stream_sample_t** output, int samples);
	virtual void device_reset();
	virtual void device_start();
private:
	/* struct describing a single playing ADPCM voice */
	struct YMZ280BVoice
	{
		uint8_t playing;          /* 1 if we are actively playing */
		bool ended;             /* indicate voice has ended in case samples_left is 0 */

		uint8_t keyon;            /* 1 if the key is on */
		uint8_t looping;          /* 1 if looping is enabled */
		uint8_t mode;             /* current playback mode */
		uint16_t fnum;            /* frequency */
		uint8_t level;            /* output level */
		uint8_t pan;              /* panning */

		uint32_t start;           /* start address, in nibbles */
		uint32_t stop;            /* stop address, in nibbles */
		uint32_t loop_start;      /* loop start address, in nibbles */
		uint32_t loop_end;        /* loop end address, in nibbles */
		uint32_t position;        /* current position, in nibbles */

		int32_t signal;           /* current ADPCM signal */
		int32_t step;             /* current ADPCM step */

		int32_t loop_signal;      /* signal at loop start */
		int32_t loop_step;        /* step at loop start */
		uint32_t loop_count;      /* number of loops so far */

		int32_t output_left;      /* output volume (left) */
		int32_t output_right;     /* output volume (right) */
		int32_t output_step;      /* step value for frequency conversion */
		int32_t output_pos;       /* current fractional position */
		int16_t last_sample;      /* last sample output */
		int16_t curr_sample;      /* current sample target */
		uint8_t irq_schedule;     /* 1 if the IRQ state is updated by timer */

		//emu_timer *timer;
	};

	void update_irq_state();
	void update_step(struct YMZ280BVoice *voice);
	void update_volumes(struct YMZ280BVoice *voice);
	int generate_adpcm(struct YMZ280BVoice *voice, int16_t *buffer, int samples);
	int generate_pcm8(struct YMZ280BVoice *voice, int16_t *buffer, int samples);
	int generate_pcm16(struct YMZ280BVoice *voice, int16_t *buffer, int samples);
	void write_to_register(int data);
	int compute_status();

	// internal state
	struct YMZ280BVoice m_voice[8];   /* the 8 voices */
	uint8_t m_current_register;         /* currently accessible register */
	uint8_t m_status_register;          /* current status register */
	uint8_t m_irq_state;                /* current IRQ state */
	uint8_t m_irq_mask;                 /* current IRQ mask */
	uint8_t m_irq_enable;               /* current IRQ enable */
	uint8_t m_keyon_enable;             /* key on enable */
	uint8_t m_ext_mem_enable;           /* external memory enable */
	uint8_t m_ext_readlatch;            /* external memory prefetched data */
	uint32_t m_ext_mem_address_hi;
	uint32_t m_ext_mem_address_mid;
	uint32_t m_ext_mem_address;         /* where the CPU can read the ROM */

	//devcb_write_line m_irq_handler;  /* IRQ callback */

	double m_master_clock;            /* master clock frequency */
	std::unique_ptr<int16_t[]> m_scratch;
#if YMZ280B_MAKE_WAVS
	void *m_wavresample;              /* resampled waveform */
#endif
};

//DECLARE_DEVICE_TYPE(YMZ280B, ymz280b_device)

#endif // MAME_SOUND_YMZ280B_H
