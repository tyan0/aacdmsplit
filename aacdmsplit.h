#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <neaacdec.h>

#define FRAME_BYTE_LIMIT	4096
#define BUFFER_SIZE		32768
#define INDEX_CHUNK_SIZE	4096

typedef struct _aacdata {
	unsigned char *data;
	size_t size;
	unsigned int *index;
	unsigned int framecnt;
	double duration;
	struct _aacheader *header;
} AACDATA;

typedef struct _aacheader {
	unsigned int frame;
	unsigned int version;
	unsigned int profile;
	int sampling_rate;
	int channel;
	struct _aacheader *next;
} AACHEADER;

enum AAC_SYNTAX_ELEMENTS {
	ID_SCE = 0x0,
	ID_CPE = 0x1,
	ID_CCE = 0x2,
	ID_LFE = 0x3,
	ID_DSE = 0x4,
	ID_PCE = 0x5,
	ID_FIL = 0x6,
	ID_END = 0x7
};

#define CRC_LEN	16
#define CRC_POLYNOMIAL 0x8005 /* CRC-16 */
#define CRC_INIT 0xffff

#define MAX_BUF 8192 /* Maximum frame_length 13bit */
#define MAX_CRC_TARGETS 16
class dualmono_splitter {
private:
	AACDATA aacdata;
	bool is_dualmono;
	struct {
		unsigned char buf[MAX_BUF];
		int len;
		int pos;
		int crc_cnt;
		struct {
			int pos0;
			int pos1;
			int len;
		} crc_target[MAX_CRC_TARGETS];
	} bitstream;
	static const int sampling_frequency_index[16];
	void errorexit(const char *msg);
	void aacrelease(void);

	/* Bit access stuff */
	unsigned long bitstoint(unsigned char *data,
			unsigned int shift, unsigned int n);
	void reset_bitstream(void);
	void setpos(int pos);
	int putbits(int n, unsigned long x);
	unsigned long getbits(int bits);

	/* CRC stuff */
	void clear_crc_target(void);
	void add_crc_target(int pos0, int pos1, int len);
	unsigned long calculate_crc(void);
	unsigned long CRC_update(unsigned long x, int bits, unsigned long crc);
	unsigned long CRC_update_bitstream(int pos0, int pos1, int len, unsigned long crc);

	/* Silent frame stuff */
	int adts_frame_silent(unsigned char *data);
	int raw_data_block_silent(int channel_configuration);
	int single_channel_element(int element_instance_tag);
	int channel_pair_element(int element_instance_tag);
	int lfe_channel_element(int element_instance_tag);
	int individual_channel_stream(int common_window);
	int ics_info(void);
	int section_data(void);
public:
	dualmono_splitter() {
		memset(&aacdata, 0, sizeof(aacdata));
		reset_bitstream();
		is_dualmono = false;
	};
	~dualmono_splitter() {
		aacrelease();
	}
	int aacopen(const char *filepath);
	bool isdualmono(void) {
		return is_dualmono;
	}
	void split(const char *filename0, const char *filename1);
};
