#include <stdio.h>
#include <string.h>
#include "aacdmsplit.h"

inline bool is_sync(unsigned char *p)
{
	return p[0] == 0xff && (p[1] & 0xf6) == 0xf0;
}

void dualmono_splitter::aacopen(const char *filepath)
{
	unsigned char fixed_header[4] = {0, 0, 0, 0};

	FILE *f = fopen(filepath, "rb");
	if (f == NULL) {
		errorexit("Cannot open AAC ADTS file for input.");
	}
	struct stat fst;
	fstat(fileno(f), &fst);
	size_t filesize = fst.st_size;

	aacdata.data = (unsigned char *)calloc(filesize, sizeof(char));
	if (aacdata.data == NULL) {
		errorexit("Faild to allocate memory.");
	}

	aacdata.index = (unsigned int *)calloc(INDEX_SIZE_INC, sizeof(int));
	if (aacdata.index == NULL) {
		errorexit("Faild to allocate memory.");
	}
	int index_size = INDEX_SIZE_INC;

	unsigned char buf[BUF_SIZE];
	size_t remain = fread(buf, 1, BUF_SIZE, f);

	int fcnt = 0;
	HEADER_CHANGE *h = NULL;
	size_t total_read = 0;
	size_t total_copied = 0;
	unsigned int frame_length = 0;
	unsigned char *p = buf;
	while (remain) {
		if (remain <= 7) {
			/* 終端はヘッダサイズより少ないことがある */
			printf("Incompete last frame (%dth frame). Discarded.\n", fcnt);
			break; /* 不完全な最終フレームは廃棄 */
		}
		if (is_sync(p)) {
			if (fcnt >= index_size) {
				index_size += INDEX_SIZE_INC;
				aacdata.index = (unsigned int *)realloc(aacdata.index,
						index_size * sizeof(unsigned int));
				if (aacdata.index == NULL) {
					errorexit("Failed to allocate memory.");
				}
			}
			aacdata.index[fcnt] = total_copied;
			frame_length = getbits(p, 30, 13);
			if (total_read + frame_length > filesize) {
				/* 終端はlengthより少ないことがある */
				printf("Incompete last frame (%dth frame). Discarded.\n", fcnt);
				break; /* 不完全な最終フレームは廃棄 */
			}
			unsigned int channel = getbits(p, 23, 3);
			if (channel == 0) is_dualmono = true;

			memcpy(aacdata.data + total_copied, p, frame_length);

			/* Fixed headerの変化をチェック */
			unsigned char tmp_header[4];
			memcpy(tmp_header, p, 4);
			tmp_header[3] &= 0xf0;
			/* 次のフレームが正しい場所に存在しない場合、現在のフレームが
			   壊れている可能性があるので、ヘッダチェックを無効化する。
			   但し、SYNCの長さ(12 bits)分残っていない時(最終フレーム)は
			   チェックを有効化する。 */
			bool next_frame_valid =
				remain < frame_length + 2 || is_sync(p + frame_length);
			if (next_frame_valid && memcmp(tmp_header, fixed_header, 4)) {
				memcpy(fixed_header, tmp_header, 4);
				HEADER_CHANGE *h_new =
					(HEADER_CHANGE *) calloc(1, sizeof(HEADER_CHANGE));
				if (h_new == NULL)
					errorexit("Failed to allocate memory.");
				h_new->frame = fcnt;
				h_new->next = NULL;
				if (h == NULL) {
					header_change = h_new;
					h = header_change;
				} else {
					h->next = h_new;
					h = h->next;
				}
			}

			fcnt ++;
			total_read += frame_length;
			total_copied += frame_length;
			p += frame_length;
			remain -= frame_length;
		} else {
			size_t orig_pos = total_read;
			printf("Incorrect %dth frame header.\n", fcnt);
			if (fcnt > 0) { /* 先頭フレーム以外 */
				/* Re-sync */
				if (!feof(f)) {
					/* 前のフレームが不完全だったかもしれないので、
					   前のフレームの syncword の直後から検索 */
					total_read -= frame_length - 2;
					fseek(f, total_read, SEEK_SET);
					remain = fread(buf, 1, BUF_SIZE, f);
					p = buf;
				}
				unsigned char *p0 = p;
				size_t remain0 = remain;
				/* Fixed Header は同じであることを期待 */
				unsigned char tmp_header[4];
				memcpy(tmp_header, p, 4);
				tmp_header[3] &= 0xf0;
				while (remain >= 4 && memcmp(p, fixed_header, 4)) {
					p ++;
					remain --;
					total_read ++;
				}
				if (remain < 4) {
					printf("Fixed-header which matches with previous frame not found.\n");
					total_read = orig_pos;
					p = p0;
					remain = remain0;
				}
			}
			/* 先頭フレーム位置をもう一度探す */
			while (remain >= 2 && !is_sync(p)) {
				p ++;
				remain --;
				total_read ++;
			}
			if (remain < 2) {
				if (!feof(f)) { /* Not EOF */
					errorexit("Cannot correct the frame start position.");
				}
			} else {
				if (fcnt > 0 && total_read < orig_pos) {
					/* 直前の損傷フレームを破棄 */
					fcnt --;
					total_copied -= frame_length;
					/* 無音フレームで代替 */
					printf("Incomplete %dth frame. Substituted with a silent frame.\n", fcnt);
					reset_bitstream();
					int len = adts_frame_silent(p) >> 3;
					memcpy(aacdata.data + total_copied, bitstream.buf, len);
					aacdata.index[fcnt] = total_copied;
					fcnt ++;
					total_copied += len;
				}
				printf("The frame position corrected. (0x%08x -> 0x%08x)\n", orig_pos, total_read);
			}
		}
		if (remain < MAX_FRAME_SIZE + 7 && !feof(f)) {
			fseek(f, total_read, SEEK_SET);
			remain = fread(buf, 1, BUF_SIZE, f);
			p = buf;
		}
	}
	fclose(f);
	if (fcnt == 0) {
		errorexit("Not a AAC ADTS file.");
	}
	aacdata.index[fcnt] = total_copied;
	aacdata.size = filesize;
	aacdata.nframe = fcnt;
	return;
}

void dualmono_splitter::split(const char *filename0, const char *filename1)
{
	/* Initialize decoder */
	NeAACDecHandle hAacDec;
	hAacDec = NeAACDecOpen();
	NeAACDecConfigurationPtr conf = NeAACDecGetCurrentConfiguration(hAacDec);
	conf->outputFormat = FAAD_FMT_16BIT;
	NeAACDecSetConfiguration(hAacDec, conf);
	unsigned long samplerate;
	unsigned char channels;
	unsigned char *p = aacdata.data;
	unsigned int frame_length = getbits(p, 30, 13);
	NeAACDecInit(hAacDec, p, frame_length, &samplerate, &channels);

	HEADER_CHANGE *h = header_change;
	FILE *f[2];
	f[0] = fopen(filename0, "wb");
	f[1] = fopen(filename1, "wb");
	if (f[0] == NULL || f[1] == NULL) {
		errorexit("Cannot open output file.");
	}
	for (unsigned int fcnt=0; fcnt < aacdata.nframe; fcnt++) {
		if (fcnt % 100 == 99 || fcnt+1 == aacdata.nframe)
			printf("%8d/%d (%d%%)\r",
					fcnt+1, aacdata.nframe, (fcnt+1)*100/aacdata.nframe);
		p = aacdata.data + aacdata.index[fcnt];
		frame_length = getbits(p, 30, 13);
		if (h->next && h->next->frame == fcnt) {
			h = h->next;
			/* Reopen decoder */
			NeAACDecClose(hAacDec);
			hAacDec = NeAACDecOpen();
			conf = NeAACDecGetCurrentConfiguration(hAacDec);
			conf->outputFormat = FAAD_FMT_16BIT;
			NeAACDecSetConfiguration(hAacDec, conf);
			NeAACDecInit(hAacDec, p, frame_length, &samplerate, &channels);
		}
		NeAACDecFrameInfo frameInfo;
		NeAACDecDecode(hAacDec, &frameInfo, p, frame_length);
		unsigned char silent[MAX_FRAME_SIZE];
		if (frameInfo.error) {
			/* 無音フレームで代替 */
			printf("\nMalformed %dth frame. Substituted with a silent frame.\n", fcnt);
			reset_bitstream();
			frame_length = adts_frame_silent(p) >> 3;
			memcpy(silent, bitstream.buf, frame_length);
			p = silent;
			NeAACDecDecode(hAacDec, &frameInfo, p, frame_length);
		}
		unsigned int version = getbits(p, 12, 1);
		unsigned int protection_absent = getbits(p, 15, 1);
		unsigned int profile = getbits(p, 16, 2);
		unsigned int sampling_rate = getbits(p, 18, 4);
		for (int i=0; i<2; i++) {
			if (channels != 0 || frameInfo.fr_ch_ele !=2) {
				/* デュアルモノ以外のフレームがもしあったら
				   両方のファイルにそのままコピーする */
				fwrite(p, 1, frame_length, f[i]);
				continue;
			}

			int ret = 0;
			int pos_len, pos_crc;

			reset_bitstream();
			ret += putbits(12, 0xFFF); /* sync word */
			ret += putbits(1,version); /* ID */
			ret += putbits(2,0); /* Layer */
			ret += putbits(1,protection_absent);
			ret += putbits(2,profile);
			ret += putbits(4,sampling_rate);
			ret += putbits(1,0); /* private bits */
			ret += putbits(3,1); /* channel configuration (MONO) */
			ret += putbits(1,0); /* original copy */
			ret += putbits(1,0); /* home */
			ret += putbits(1,0); /* copyright_identification_bit */
			ret += putbits(1,0); /* copyright_identification_start */
			pos_len = bitstream.pos;
			ret += putbits(13,0); /* frame_length (dummy, will be set later) */
			ret += putbits(11,0x7FF); /* adts_buffer_fullness */
			ret += putbits(2,0); /* number_of_raw_data_blocks_in_frame */

			clear_crc_target();
			add_crc_target(0, bitstream.pos, ret); /* all header */

			/* adts_error_check() */
			pos_crc = bitstream.pos;
			if (protection_absent == 0) {
				ret += putbits(16, 0); /* CRC (dummy, will be set later) */
			}
	
			/* copy SCE */
			int start_bits = frameInfo.element_start[i];
			int end_bits = frameInfo.element_end[i];

			int id_syn_ele = getbits(p, start_bits, 3);
			if (id_syn_ele != ID_SCE) {
				printf("ID: %d (expected %d)\n", id_syn_ele, ID_SCE);
			}
			ret += putbits(3, ID_SCE);
			int element_instance_tag = getbits(p, start_bits+3, 4);
			if (element_instance_tag != i) {
				printf("element_instance_tag: %d (expected %d)\n",
						element_instance_tag, i);
			}
			int pos_sce = bitstream.pos;
			ret += putbits(4, 0); /* element_instance_tag */
			int pos;
			for (pos = start_bits+7; pos + 32 <= end_bits; pos += 32) {
				int d = getbits(p, pos, 32);
				ret += putbits(32, d);
			}
			int remain = end_bits - pos;
			if (remain > 0) {
				int d = getbits(p, pos, remain);
				ret += putbits(remain, d);
			}
			add_crc_target(pos_sce, bitstream.pos, 192); /* SCE */

			ret += putbits(3,ID_END);
			/* Byte align */
			if (bitstream.len & 7) {
				ret += putbits(8 - (bitstream.len & 7), 0);
			}

			setpos(pos_len); putbits(13, ret>>3); /* frame_length */

			/* Calculate CRC */
			if (protection_absent == 0) {
				setpos(pos_crc); putbits(16, calculate_crc()); 
			}

			fwrite(bitstream.buf, 1, ret>>3, f[i]);
		}
	}
	printf("\n");
	NeAACDecClose(hAacDec);
	fclose(f[0]);
	fclose(f[1]);
}

void dualmono_splitter::errorexit(const char *errorstr)
{
	fprintf(stderr, "Error: %s\n", errorstr);
	exit(-1);
}

void dualmono_splitter::aacrelease(void)
{
	if (aacdata.data) free(aacdata.data);
	if (aacdata.index) free(aacdata.index);
	HEADER_CHANGE *h = header_change;
	while (h) {
		HEADER_CHANGE *next = h->next;
		free(h);
		h = next;
	}
	memset(&aacdata, 0, sizeof(aacdata));
	header_change = NULL;
	return;
}

void dualmono_splitter::reset_bitstream(void)
{
	memset(&bitstream, 0, sizeof(bitstream));
}

void dualmono_splitter::setpos(int pos)
{
	if (pos >= (MAX_FRAME_SIZE*8)) errorexit("setpos(): exceeded the buffer.");
	bitstream.pos = pos;
}

int dualmono_splitter::putbits(int n, unsigned long x)
{
	int ret = 0;
	int b = 8 - (bitstream.pos & 0x07);
	int i = bitstream.pos >> 3;
	if (bitstream.pos < 0 || bitstream.pos + n > (MAX_FRAME_SIZE*8)) {
		errorexit("putbits(): range exceeded the buffer.");
	}
	while (n >= b) {
		bitstream.buf[i] &= ~((1<<b) - 1);
		bitstream.buf[i] |= (x>>(n-b)) & ((1<<b) -1);
		bitstream.pos += b;
		ret += b;
		n -= b;
		i ++;
		b = 8;
	}
	if (n>0) {
		bitstream.buf[i] &= ~(((1<<n) - 1) << (b-n));
		bitstream.buf[i] |= (x & ((1<<n) - 1)) << (b-n);
		bitstream.pos += n;
		ret += n;
	}
	if (bitstream.len < bitstream.pos) bitstream.len = bitstream.pos;
	return ret;
}

unsigned long dualmono_splitter::getbits(unsigned char *p, int pos, int bits)
{
	if (bits > 32) errorexit("getbits(): bits > 32 not supported.");

	unsigned long ret = 0;
	int b = 8 - (pos & 0x07);
	int i = pos >> 3;

	while (bits >= b) {
		ret <<= b;
		ret |= p[i] & ((1<<b)-1);
		pos += b;
		bits -= b;
		i ++;
		b = 8;
	}
	if (bits > 0) {
		ret <<= bits;
		ret |= ((p[i] >> (b - bits)) & ((1<<bits) -1));
		pos += bits;
	}
	return ret;
}

unsigned long dualmono_splitter::getbits(int bits)
{
	if (bitstream.pos < 0 || bitstream.pos + bits > bitstream.len) {
		errorexit("getbits(): range exceeded the buffer.");
	}
	unsigned long ret = getbits(bitstream.buf, bitstream.pos, bits);
	bitstream.pos += bits;
	return ret;
}

void dualmono_splitter::clear_crc_target(void)
{
	bitstream.crc_cnt = 0;
}

void dualmono_splitter::add_crc_target(int pos0, int pos1, int len)
{
	if (bitstream.crc_cnt >= MAX_CRC_TARGETS)
		errorexit("add_crc_target(): Too many CRC targets.");
	bitstream.crc_target[bitstream.crc_cnt].pos0 = pos0;
	bitstream.crc_target[bitstream.crc_cnt].pos1 = pos1;
	bitstream.crc_target[bitstream.crc_cnt].len = len;
	bitstream.crc_cnt ++;
}

unsigned long dualmono_splitter::calculate_crc(void)
{
	int i;
	unsigned long crc = CRC_INIT;
	int pos0 = bitstream.pos;
	for (i=0; i<bitstream.crc_cnt; i++) {
		crc = CRC_update_bitstream(
			bitstream.crc_target[i].pos0,
			bitstream.crc_target[i].pos1,
			bitstream.crc_target[i].len, crc);
	}
	setpos(pos0);
	return crc;
}

unsigned long dualmono_splitter::CRC_update(unsigned long x, int bits, unsigned long crc)
{
	int i;
	for (i=0; i<bits; i++) {
		int fb = ((x >> (bits-i-1)) ^ (crc >> (CRC_LEN-1))) & 1;
		crc <<= 1;
		if (fb) crc ^= CRC_POLYNOMIAL;
	}
	return crc&((1<<CRC_LEN)-1);
}

unsigned long dualmono_splitter::CRC_update_bitstream(int pos0, int pos1, int len, unsigned long crc)
{
	int i;
	int pos2 = pos0 + len;
	const int m = 32;

	setpos(pos0);
	if (pos1 >= pos2) {
		for (i=0; i<len/m; i++) {
			crc = CRC_update( getbits(m), m, crc);
			pos0 += m;
		}
		if (pos2 > pos0) {
			crc = CRC_update( getbits(pos2-pos0), pos2-pos0, crc);
			pos0 = pos1;
		}
	} else {
		while (pos1 > pos0+m) {
			crc = CRC_update( getbits(m), m, crc);
			pos0 += m;
		}
		if (pos1 > pos0) {
			crc = CRC_update( getbits(pos1-pos0), pos1-pos0, crc);
			pos0 = pos1;
		}
		while (pos2 > pos0+m) {
			crc = CRC_update( 0, m, crc);
			pos0 += m;
		}
		if ( pos2 > pos0) {
			crc = CRC_update( 0, pos2-pos0, crc);
			pos0 = pos2;
		}
	}
	return crc;
}

int dualmono_splitter::adts_frame_silent(unsigned char *data)
{
	int ret = 0;
	int pos_len, pos_crc;
	int protection_absent = getbits(data, 15, 1);
	int channel_configuration = getbits(data, 23, 3);
	int number_of_raw_data_blocks_in_frame = getbits(data, 54, 2);

	/* adts_fixed_header() */
	ret += putbits(28, (data[0]<<20)|(data[1]<<12)|(data[2]<<4)|(data[3]>>4));

	/* adts_variable_header() */
	ret += putbits(1, 0); /* copyright_identification_bit */
	ret += putbits(1, 0); /* copyright_identification_start */
	pos_len = bitstream.pos;
	ret += putbits(13, 0); /* frame_length (dummy, will be set later) */
	ret += putbits(11, 0x7ff); /* adts_buffer_fullness */
	ret += putbits(2, number_of_raw_data_blocks_in_frame);

	if (number_of_raw_data_blocks_in_frame == 0) {
		clear_crc_target();
		add_crc_target(0, bitstream.pos, ret); /* all header */

		/* adts_error_check() */
		pos_crc = bitstream.pos;
		if (protection_absent == 0) {
			ret += putbits(16, 0); /* CRC (dummy, will be set later) */
		}
	
		ret += raw_data_block_silent(channel_configuration);
	
		setpos(pos_len); putbits(13, ret>>3); /* frame_length */
	
		if (protection_absent == 0) {
			setpos(pos_crc); putbits(16, calculate_crc()); 
		}
	} else {
		int i;
		int raw_data_block_position[4];
		int pos_pos;

		pos_pos = bitstream.pos;
		if (protection_absent == 0) {
			for (i=1; i<number_of_raw_data_blocks_in_frame + 1; i++) {
				ret += putbits(16, 0); /* raw_data_block_position[] (dummy, will be set later) */
			}
			ret += putbits(16, 0); /* CRC (dummy, will be set later) */
		}

		for (i=0; i<number_of_raw_data_blocks_in_frame + 1; i++) {
			raw_data_block_position[i] = bitstream.pos >> 3;
			clear_crc_target();
			ret += raw_data_block_silent(channel_configuration);
			if (protection_absent == 0) {
				ret += putbits(16, calculate_crc());
			}
		}

		setpos(pos_len); putbits(13, ret>>3); /* frame_length */

		if (protection_absent == 0) {
			setpos(pos_pos);
			for (i=1; i<number_of_raw_data_blocks_in_frame + 1; i++) {
				putbits(16, raw_data_block_position[i] - raw_data_block_position[0]);
			}
			clear_crc_target();
			add_crc_target(0, bitstream.pos, bitstream.pos); /* all header and raw_data_block_position[] */
			putbits(16, calculate_crc());
		}
	}

	return ret;
}

int dualmono_splitter::raw_data_block_silent(int channel_configuration)
{
#define SCE(x) ret += putbits(3, ID_SCE)/*id_syn_ele*/; ret += single_channel_element(x)
#define CPE(x) ret += putbits(3, ID_CPE)/*id_syn_ele*/; ret += channel_pair_element(x)
#define LFE(x) ret += putbits(3, ID_LFE)/*id_syn_ele*/; ret += lfe_channel_element(x)
#define TERM() ret += putbits(3, ID_END)/*id_syn_ele*/;
	int ret = 0;
	switch (channel_configuration) {
	case 0: /* AAC デフォルト規定以外 */
		/* 1/0+1/0 とは限らないが、まじめにパーサを組まないと
		   正しいチャネル構成が分からない。ARIB STD-B32 では
		   1/0+1/0(SCE+SCE), 2/1(CPE+SCE), 2/2(CPE+CPE) が
		   可能だが、推奨音声モードではない 2/1, 2/2 は捨てた。 */
		SCE(0); SCE(1); break; /* デュアルモノ 1/0+1/0 */
	case 1: /* モノラル */
		SCE(0); break;
	case 2: /* ステレオ */
		CPE(0); break;
	case 3:
		SCE(0); CPE(0); break;
	case 4:
		SCE(0); CPE(0); SCE(1); break;
	case 5:
		SCE(0); CPE(0); CPE(1); break;
	case 6: /* 5.1ch */
		SCE(0); CPE(0); CPE(1); LFE(0); break;
	case 7:
		SCE(0); CPE(0); CPE(1); CPE(2); LFE(0); break;
		break;
	}
	TERM();

	/* byte_alignment() */
	if (bitstream.len & 7) {
		ret += putbits(8 - (bitstream.len & 7), 0);
	}
	return ret;
}

int dualmono_splitter::single_channel_element(int element_instance_tag)
{
	int ret = 0;
	int pos0 = bitstream.pos;

	ret += putbits(4, element_instance_tag);
	ret += individual_channel_stream(0);

	add_crc_target(pos0, bitstream.pos, 192);
	return ret;
}

int dualmono_splitter::channel_pair_element(int element_instance_tag)
{
	const int common_window = 1;
	int ret = 0;
	int pos0, pos1;

	pos0 = bitstream.pos;
	ret += putbits(4, element_instance_tag);
	ret += putbits(1, common_window); /* common_window */
	if (common_window) {
		ret += ics_info();
#ifdef NO_MS_MASK
		ret += putbits(2, 0); /* ms_mask_present */
#else
		ret += putbits(2, 1); /* ms_mask_present */
#ifndef ZERO_MAX_SFB
		ret += putbits(1, 0); /* ms_used[0][0] */
#endif
#endif
	}
	ret += individual_channel_stream(common_window);
	pos1 = bitstream.pos;
	ret += individual_channel_stream(common_window);

	add_crc_target(pos0, bitstream.pos, 192);
	add_crc_target(pos1, bitstream.pos, 128);
	return ret;
}

int dualmono_splitter::lfe_channel_element(int element_instance_tag)
{
	int ret = 0;
	int pos0 = bitstream.pos;

	ret += putbits(4, element_instance_tag);
	ret += individual_channel_stream(0);

	add_crc_target(pos0, bitstream.pos, 192);
	return ret;
}

int dualmono_splitter::individual_channel_stream(int common_window)
{
	int ret = 0;
	ret += putbits(8, 100); /* global_gain */
	if (! common_window) {
		ret += ics_info();
	}
	ret += section_data();
	/* ret += scale_factor_data(); */ /* not necessary for silent data */

	ret += putbits(1, 0); /* pulse_data_present */
	ret += putbits(1, 0); /* tns_data_present */
	ret += putbits(1, 0); /* gain_control_data_present */

	/* ret += spectral_data(); */ /* not necessary for silent data */
	return ret;
}

int dualmono_splitter::ics_info(void)
{
	int ret = 0;
	ret += putbits(1, 0); /* ics_reserved_bit */
	ret += putbits(2, 0); /* window_sequence */
	ret += putbits(1, 0); /* window_shape */
#ifdef ZERO_MAX_SFB
	ret += putbits(6, 0); /* max_sfb */
#else
	ret += putbits(6, 1); /* max_sfb */
#endif
	ret += putbits(1, 0); /* predictor_data_present */
	return ret;
}

int dualmono_splitter::section_data(void)
{
	int ret = 0;
#ifndef ZERO_MAX_SFB
	ret += putbits(4, 0); /* sect_cb[0][0] */
	ret += putbits(5, 1); /* sect_len_incr */
#endif
	return ret;
}

void usage(void)
{
	fprintf(stderr, "aacdmsplit <AAC filename>\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		usage();
	}
	const char *aacfile = argv[1];
	dualmono_splitter splitter;
	splitter.aacopen(aacfile);
	if (!splitter.isdualmono()) {
		fprintf(stderr,"Not a dual-mono ADTS file.\n");
		exit(-1);
	}

	char outputfile[2][PATH_MAX];
	for (int i=0; i<2; i++) {
		char suffix[8];
		memset(outputfile[i], 0, PATH_MAX);
		strncpy(outputfile[i], aacfile, PATH_MAX-8);
		char *p = strrchr(outputfile[i],'.');
		sprintf(suffix, " SCE%d", i);
		if (p) {
			memmove(p+strlen(suffix), p, strlen(p));
			memcpy(p, suffix, strlen(suffix));
		} else {
			strcat(outputfile[i], suffix);
		}
	}
	splitter.split(outputfile[0], outputfile[1]);

	return 0;
}
