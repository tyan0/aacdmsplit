#include <stdio.h>
#include <string.h>
#include "aacdmsplit.h"

const int dualmono_splitter::sampling_frequency_index[16] = {
	96000, 88200, 64000, 48000,
	44100, 32000, 24000, 22050,
	16000, 12000, 11025, 8000,
	-12, -13, -14, -15};

int dualmono_splitter::aacopen(const char *filepath)
{
	int ret = 1;
	unsigned int aacframecnt = 0, indexchunk = INDEX_CHUNK_SIZE;
	unsigned int readbyte = 0, copybyte = 0;
	unsigned int channel = 0xFF, tmpchannel, aac_frame_length = 0;
	unsigned char buffer[BUFFER_SIZE], *p, *plast;
	AACHEADER *aacheader = NULL, *aacheadertop = NULL;
	FILE *f;
	struct stat fst;
	unsigned int filesize, readchunk = BUFFER_SIZE;
	unsigned char fixed_header[4] = {0xff, 0xf0};
	unsigned char fixed_header_mask[4] = {0xff, 0xf6};

	if (!filepath)
		return 0;
	f = fopen(filepath, "rb");
	if (f == NULL) {
		errorexit("AAC ファイルの読み込みに失敗しました。");
	}

	fstat(fileno(f), &fst);
	filesize = fst.st_size;
	aacdata.data = (unsigned char *)calloc(filesize, sizeof(char));
	aacdata.index = (unsigned int *)calloc(indexchunk, sizeof(int));
	aacdata.duration = 0;
	if (aacdata.data == NULL || aacdata.index == NULL)
		errorexit("メモリ確保に失敗しました。");
	readchunk = fread(buffer, 1, readchunk, f);
	while (readchunk > 0) {
		p = buffer;
		plast = p + readchunk - 7;
		while (p < plast) {
			if (*p == 0xFF && (*(p+1) & 0xF6) == 0xF0) {
				memcpy(fixed_header, p, 4);
				memcpy(fixed_header_mask, "\xff\xff\xff\xf0", 4);
				aacdata.index[aacframecnt] = copybyte;
				aac_frame_length = bitstoint(p + 3, 6, 13);
				if (readbyte + aac_frame_length > filesize) {
					/* 終端はlengthより少ないことがある */
					printf("最終フレーム(第%uフレーム)が不完全です。廃棄します。\n", aacframecnt);
					break; /* 不完全な最終フレームは廃棄 */
				}
				if ( readbyte + aac_frame_length + 2 <= filesize ) {
					/* 次のフレームのヘッダチェック */
					/* 信頼できないフレームのヘッダは無視 */
					if ( *(p + aac_frame_length) == 0xFF
							&& (*(p + aac_frame_length + 1) & 0xF6) == 0xF0) {
						int number_of_raw_data_blocks_in_frame = bitstoint(p + 6, 6, 2);
						int sampling_rate = sampling_frequency_index[bitstoint(p, 18, 4)];
						double cur_frame_rate =
							(double)sampling_rate / (number_of_raw_data_blocks_in_frame + 1) / 1024;
						if (sampling_rate>0) {
							aacdata.duration += 1 / cur_frame_rate;
						}
						tmpchannel = bitstoint(p, 23, 3);
						if (tmpchannel == 0) is_dualmono = true;
						if (channel != tmpchannel) {
							AACHEADER *tmpaacheader = (AACHEADER *)calloc(1, sizeof(AACHEADER));
							if (tmpaacheader == NULL)
								errorexit("メモリ確保に失敗しました。");
							tmpaacheader->frame = aacframecnt;
							tmpaacheader->version = bitstoint(p, 12, 1);
							tmpaacheader->profile = bitstoint(p, 16, 2);
							tmpaacheader->sampling_rate = bitstoint(p, 18, 4);
							tmpaacheader->channel = channel = tmpchannel;
							tmpaacheader->next = NULL;
							if (!aacheader) {
								aacheadertop = tmpaacheader;
							} else {
								aacheader->next = tmpaacheader;
							}
							aacheader = tmpaacheader;
						}
					}
				}
				memcpy(aacdata.data + copybyte, p, aac_frame_length);
				aacframecnt++;
				if (aacframecnt >= indexchunk) {
					indexchunk += INDEX_CHUNK_SIZE;
					aacdata.index = (unsigned int *)realloc(aacdata.index, indexchunk * sizeof(unsigned int));
					if (aacdata.index == NULL) {
						errorexit("メモリ確保に失敗しました。");
					}
				}
				readbyte += aac_frame_length;
				copybyte += aac_frame_length;
				if (filesize <= readbyte)
					break;
				else if ((p + aac_frame_length > plast - FRAME_BYTE_LIMIT) && readchunk >= BUFFER_SIZE) {
					fseek(f, readbyte, SEEK_SET);
					break;
				}
				p += aac_frame_length; /* フレーム分移動 */
			} else {
				unsigned int orig_pos = readbyte;
				printf("%u フレーム目のヘッダー情報が正しくありません。\n", aacframecnt);
				if (aacframecnt > 0) { /* 先頭フレーム以外 */
					unsigned int readbyte0;
					unsigned char *p0;
					/* Re-sync */
					if (readchunk >= BUFFER_SIZE /* Not EOF */) {
		 				/* 前のフレームが不完全だったかもしれないので、
						   前のフレームの syncword の直後から検索 */
						readbyte -= aac_frame_length - 2;
						fseek(f, readbyte, SEEK_SET);
						readchunk = fread(buffer, 1, readchunk, f);
						p = buffer;
						plast = p + readchunk - 7;
					}
					readbyte0 = readbyte;
					p0 = p;
					/* Fixed Header は同じであることを期待 */
					while (p < plast && !(
						(p[0] & fixed_header_mask[0])
							== (fixed_header[0] & fixed_header_mask[0]) &&
						(p[1] & fixed_header_mask[1])
							== (fixed_header[1] & fixed_header_mask[1]) &&
						(p[2] & fixed_header_mask[2])
							== (fixed_header[2] & fixed_header_mask[2]) &&
						(p[3] & fixed_header_mask[3])
							== (fixed_header[3] & fixed_header_mask[3])
					)) {
						p++;
						readbyte ++;
					}
					if (p >= plast) {
						printf("直前のフレームと同じFixed Headerが見つかりません。\n");
						readbyte = readbyte0;
						p = p0;
					}
				}
				/* 先頭フレーム位置を探す */
				while (p < plast
						&& !(*p == 0xFF && (*(p+1)&0xF6) == 0xF0)) {
					p++;
					readbyte ++;
				}
				if (p >= plast) {
					if (readchunk >= BUFFER_SIZE) { /* Not EOF */
						fclose(f);
						errorexit("フレーム先頭位置を修正できませんでした。");
					}
				} else {
					if (aacframecnt >= 1 && readbyte < orig_pos) {
						/* 直前の損傷フレームを破棄 */
						int len;
						aacframecnt--;
						copybyte -= aac_frame_length;
						/* 無音フレームで代替 */
						printf("%d フレーム目が不完全です。無音フレームで代替します。\n", aacframecnt);
						reset_bitstream();
						len = adts_frame_silent(p) >> 3;
						memcpy(aacdata.data + copybyte, bitstream.buf, len);
						aacdata.index[aacframecnt] = copybyte;
						aacframecnt++;
						copybyte += len;
					}
					printf("フレーム先頭位置を修正 (0x%08x → 0x%08x)\n", orig_pos, readbyte);
				}
				ret = 2;
			}
		}
		readchunk = fread(buffer, 1, readchunk, f);
	}
	fclose(f);
	if (aacframecnt == 0) {
		errorexit("AAC ADTS ファイルではありません。");
	}
	aacdata.index[aacframecnt] = copybyte;
	aacdata.size = filesize;
	aacdata.framecnt = aacframecnt;
	aacdata.header = aacheadertop;
	return ret;
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
	unsigned int aac_frame_length = bitstoint(p + 3, 6, 13);
	NeAACDecInit(hAacDec, p, aac_frame_length, &samplerate, &channels);

	AACHEADER *h = aacdata.header;
	FILE *f[2];
	f[0] = fopen(filename0, "wb");
	f[1] = fopen(filename1, "wb");
	if (f[0] == NULL || f[1] == NULL) {
		errorexit("Cannot open output file.");
	}
	for (unsigned int fcnt=0; fcnt < aacdata.framecnt; fcnt++) {
		if (fcnt % 100 == 99 || fcnt+1 == aacdata.framecnt)
			printf("%8d/%d (%d%%)\r",
					fcnt+1, aacdata.framecnt, (fcnt+1)*100/aacdata.framecnt);
		p = aacdata.data + aacdata.index[fcnt];
		aac_frame_length = bitstoint(p + 3, 6, 13);
		if (h->next && h->next->frame == fcnt) {
			h = h->next;
			/* Reopen decoder */
			NeAACDecClose(hAacDec);
			hAacDec = NeAACDecOpen();
			conf = NeAACDecGetCurrentConfiguration(hAacDec);
			conf->outputFormat = FAAD_FMT_16BIT;
			NeAACDecSetConfiguration(hAacDec, conf);
			NeAACDecInit(hAacDec, p, aac_frame_length, &samplerate, &channels);
		}
		NeAACDecFrameInfo frameInfo;
		NeAACDecDecode(hAacDec, &frameInfo, p, aac_frame_length);
		if (frameInfo.error) {
			/* 無音フレームで代替 */
			reset_bitstream();
			aac_frame_length = adts_frame_silent(p) >> 3;
			NeAACDecDecode(hAacDec, &frameInfo, bitstream.buf, aac_frame_length);
			p = bitstream.buf;
		}
		int protection_absent = bitstoint(p, 15, 1);
		for (int i=0; i<2; i++) {
			if (channels != 0 || frameInfo.fr_ch_ele !=2) {
				/* デュアルモノ以外のフレームがもしあったら
				   両方のファイルにそのままコピーする */
				fwrite(p, 1, aac_frame_length, f[i]);
				continue;
			}

			int ret = 0;
			int pos_len, pos_crc;
			int start_bits = frameInfo.element_start[i];
			int end_bits = frameInfo.element_end[i];

			reset_bitstream();
			ret += putbits(12, 0xFFF); /* sync word */
			ret += putbits(1,1); /* ID */
			ret += putbits(2,0); /* Layer */
			ret += putbits(1,protection_absent);
			ret += putbits(2,h->profile);
			ret += putbits(4,h->sampling_rate);
			ret += putbits(1,0); /* private bits */
			ret += putbits(3,1); /* channel configuration (MONO) */
			ret += putbits(1,0); /* original copy */
			ret += putbits(1,0); /* home */
			ret += putbits(1,0); /* copyright_identification_bit */
			ret += putbits(1,0); /* copyright_identification_start */
			pos_len = bitstream.pos;
			ret += putbits(13,0); /* frame_length (dummy, will be set later) */
			ret += putbits(11,0x7ff); /* adts_buffer_fullness */
			ret += putbits(2,0); /* number_of_raw_data_blocks_in_frame */

			clear_crc_target();
			add_crc_target(0, bitstream.pos, ret); /* all header */

			/* adts_error_check() */
			pos_crc = bitstream.pos;
			if (protection_absent == 0) {
				ret += putbits(16, 0); /* CRC (dummy, will be set later) */
			}
	
			/* copy SCE */
			int id_syn_ele = bitstoint(p + (start_bits>>3), start_bits&7, 3);
			if (id_syn_ele != ID_SCE) {
				printf("ID: %d (expected %d)\n", id_syn_ele, ID_SCE);
			}
			int pos_sce = bitstream.pos + 3;
			int pos;
			for (pos = start_bits; pos + 16 <= end_bits; pos += 16) {
				int d = bitstoint(p + (pos>>3), pos&7, 16);
				ret += putbits(16, d);
			}
			int remain = end_bits - pos;
			if (remain > 0) {
				int d = bitstoint(p + (pos>>3), pos&7, remain);
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

			fwrite(&bitstream.buf, 1, ret>>3, f[i]);
		}
	}
	printf("\n");
	NeAACDecClose(hAacDec);
	fclose(f[0]);
	fclose(f[1]);
}

void dualmono_splitter::errorexit(const char *errorstr)
{
	fprintf(stderr, "エラー: %s\n", errorstr);
	exit(1);
}

unsigned long dualmono_splitter::bitstoint(unsigned char *data,
	unsigned int shift, unsigned int n)
{
	unsigned long ret;

	memcpy(&ret, data, sizeof(unsigned long));
	ret = (ret << 24) | (ret << 8 & 0x00FF0000) | (ret >> 8 & 0x0000FF00) | (ret >> 24 & 0x000000FF);
	ret = (ret >> (32 - shift - n));
	ret &= 0xFFFFFFFF & ((1 << n) - 1);
	return ret;
}

void dualmono_splitter::aacrelease(void)
{
	AACHEADER *aacheader, *aacheadernext;

	if (aacdata.data) free(aacdata.data);
	if (aacdata.index) free(aacdata.index);
	aacheader = aacdata.header;
	while (aacheader) {
		aacheadernext = aacheader->next;
		free(aacheader);
		aacheader = aacheadernext;
	}
	return;
}

void dualmono_splitter::reset_bitstream(void)
{
	memset(&bitstream, 0, sizeof(bitstream));
}

void dualmono_splitter::setpos(int pos)
{
	if (pos >= (MAX_BUF*8)) errorexit("setpos(): exceeded the buffer.");
	bitstream.pos = pos;
}

int dualmono_splitter::putbits(int n, unsigned long x)
{
	int ret = 0;
	int b = 8 - (bitstream.pos & 0x07);
	int i = bitstream.pos >> 3;
	if (bitstream.pos < 0 || bitstream.pos + n > (MAX_BUF*8)) {
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

unsigned long dualmono_splitter::getbits(int bits)
{
	unsigned long ret = 0;
	int b = 8 - (bitstream.pos & 0x07);
	int i = bitstream.pos >> 3;

	if (bits > 32) errorexit("getbits(): bits > 32 not supported.");
	if (bitstream.pos < 0 || bitstream.pos + bits > bitstream.len) {
		errorexit("getbits(): range exceeded the buffer.");
	}
	ret = 0;
	while (bits >= b) {
		ret <<= b;
		ret |= bitstream.buf[i] & ((1<<b)-1);
		bitstream.pos += b;
		bits -= b;
		i ++;
		b = 8;
	}
	if (bits > 0) {
		ret <<= bits;
		ret |= ((bitstream.buf[i] >> (b - bits)) & ((1<<bits) -1));
		bitstream.pos += bits;
	}
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
	int protection_absent = bitstoint(data, 15, 1);
	int channel_configuration = bitstoint(data, 23, 3);
	int number_of_raw_data_blocks_in_frame = bitstoint(data+6, 6, 2);

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
				ret += putbits(16, 0); /* raw_data_block_position[] (dummy, will be set later*/
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
	fprintf(stderr, "aacdmsplit <AAC filenamee>\n");
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
		sprintf(suffix, " SCE#%d", i);
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
