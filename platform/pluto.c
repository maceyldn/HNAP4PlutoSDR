/*
 * pluto.c
 *
 *  Created on: 26.12.2019
 *      Author: lukas
 */


#include "pluto.h"
#include <errno.h>
#include "../phy/phy_config.h"
#include "../util/log.h"
#include <stdio.h>
#include <string.h>
#include <iio.h>
#include <unistd.h>

/* helper macros */
#define MHZ(x) ((long long)(x*1000000.0 + .5))
#define KHZ(x) ((long long)(x*1000.0 + .5))

#define ASSERT(expr) { \
	if (!(expr)) { \
		(void) fprintf(stderr, "assertion failed (%s:%d)\n", __FILE__, __LINE__); \
		(void) abort(); \
	} \
}

/* RX is input, TX is output */
enum iodev { RX, TX };

/* common RX and TX streaming params */
struct stream_cfg {
	long long bw_hz; // Analog banwidth in Hz
	long long fs_hz; // Baseband sample rate in Hz
	long long lo_hz; // Local oscillator frequency in Hz
	const char* rfport; // Port name
};

/* static scratch mem for strings */
static char tmpstr[64];

// Pluto platform
struct pluto_data_s {
    /* IIO structs required for streaming */
    struct iio_context *ctx;
    struct iio_channel *rx0_i;
    struct iio_channel *rx0_q;
    struct iio_channel *tx0_i;
    struct iio_channel *tx0_q;
    struct iio_buffer *rxbuf;
    struct iio_buffer *txbuf;

    // AD9361 phy device
    struct iio_device * ad9361_phy;

    // Streaming devices
    struct iio_device *tx;
    struct iio_device *rx;

    // Stream configurations
    struct stream_cfg rxcfg;
    struct stream_cfg txcfg;

    // TDD or FDD mode
    enum duplex_type duplex_mode;
    uint ptt_assertion_delay;
};
typedef struct  pluto_data_s* pluto_data;

/* cleanup and exit */
void shutdown(platform hw)
{
    pluto_data pluto = (pluto_data)hw->data;

	printf("* Destroying buffers\n");
	if (pluto->rxbuf) { iio_buffer_destroy(pluto->rxbuf); }
	if (pluto->txbuf) { iio_buffer_destroy(pluto->txbuf); }

	printf("* Disabling streaming channels\n");
	if (pluto->rx0_i) { iio_channel_disable(pluto->rx0_i); }
	if (pluto->rx0_q) { iio_channel_disable(pluto->rx0_q); }
	if (pluto->tx0_i) { iio_channel_disable(pluto->tx0_i); }
	if (pluto->tx0_q) { iio_channel_disable(pluto->tx0_q); }

	printf("* Destroying context\n");
	if (pluto->ctx) { iio_context_destroy(pluto->ctx); }
}


/* check return value of attr_write function */
static void errchk(int v, const char* what) {
	 if (v < 0) { fprintf(stderr, "Error %d writing to channel \"%s\"\nvalue may not be supported.\n", v, what);}
}

/* write attribute: long long int */
static void wr_ch_lli(struct iio_channel *chn, const char* what, long long val)
{
	errchk(iio_channel_attr_write_longlong(chn, what, val), what);
}

/* write attribute: string */
static void wr_ch_str(struct iio_channel *chn, const char* what, const char* str)
{
	errchk(iio_channel_attr_write(chn, what, str), what);
}

/* helper function generating channel names */
static char* get_ch_name(const char* type, int id)
{
	snprintf(tmpstr, sizeof(tmpstr), "%s%d", type, id);
	return tmpstr;
}

/* returns ad9361 phy device */
static struct iio_device* get_ad9361_phy(struct iio_context *ctx)
{
	struct iio_device *dev =  iio_context_find_device(ctx, "ad9361-phy");
	ASSERT(dev && "No ad9361-phy found");
	return dev;
}

/* finds AD9361 streaming IIO devices */
static bool get_ad9361_stream_dev(struct iio_context *ctx, enum iodev d, struct iio_device **dev)
{
	switch (d) {
	case TX: *dev = iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc"); return *dev != NULL;
	case RX: *dev = iio_context_find_device(ctx, "cf-ad9361-lpc");  return *dev != NULL;
	default: ASSERT(0); return false;
	}
}

/* finds AD9361 streaming IIO channels */
static bool get_ad9361_stream_ch(struct iio_context *ctx, enum iodev d, struct iio_device *dev, int chid, struct iio_channel **chn)
{
	*chn = iio_device_find_channel(dev, get_ch_name("voltage", chid), d == TX);
	if (!*chn)
		*chn = iio_device_find_channel(dev, get_ch_name("altvoltage", chid), d == TX);
	return *chn != NULL;
}

/* finds AD9361 phy IIO configuration channel with id chid */
static bool get_phy_chan(struct iio_context *ctx, enum iodev d, int chid, struct iio_channel **chn)
{
	switch (d) {
	case RX: *chn = iio_device_find_channel(get_ad9361_phy(ctx), get_ch_name("voltage", chid), false); return *chn != NULL;
	case TX: *chn = iio_device_find_channel(get_ad9361_phy(ctx), get_ch_name("voltage", chid), true);  return *chn != NULL;
	default: ASSERT(0); return false;
	}
}

/* finds AD9361 local oscillator IIO configuration channels */
static bool get_lo_chan(struct iio_context *ctx, enum iodev d, struct iio_channel **chn)
{
	switch (d) {
	 // LO chan is always output, i.e. true
	case RX: *chn = iio_device_find_channel(get_ad9361_phy(ctx), get_ch_name("altvoltage", 0), true); return *chn != NULL;
	case TX: *chn = iio_device_find_channel(get_ad9361_phy(ctx), get_ch_name("altvoltage", 1), true); return *chn != NULL;
	default: ASSERT(0); return false;
	}
}

/* applies streaming configuration through IIO */
bool cfg_ad9361_streaming_ch(struct iio_context *ctx, struct stream_cfg *cfg, enum iodev type, int chid)
{
	struct iio_channel *chn = NULL;

	// Configure phy and lo channels
	printf("* Acquiring AD9361 phy channel %d\n", chid);
	if (!get_phy_chan(ctx, type, chid, &chn)) {	return false; }
	wr_ch_str(chn, "rf_port_select",     cfg->rfport);
	wr_ch_lli(chn, "rf_bandwidth",       cfg->bw_hz);
	wr_ch_lli(chn, "sampling_frequency", cfg->fs_hz);

	// Configure LO channel
	printf("* Acquiring AD9361 %s lo channel\n", type == TX ? "TX" : "RX");
	if (!get_lo_chan(ctx, type, &chn)) { return false; }
	wr_ch_lli(chn, "frequency", cfg->lo_hz);
	return true;
}

// Push samples to TX buffer object
// returns the number of samples that have been pushed
int pluto_prep_tx(platform hw, float complex* buf_tx, uint offset, uint num_samples)
{
    pluto_data pluto = (pluto_data)hw->data;
	char *p_dat, *p_end, *p_start;
	ptrdiff_t p_inc;


	// WRITE: Get pointers to TX buf and write IQ to TX buf port 0
	p_inc = iio_buffer_step(pluto->txbuf);
	p_start = iio_buffer_first(pluto->txbuf, pluto->tx0_i) + offset*p_inc;
	p_end = iio_buffer_end(pluto->txbuf);
	uint i=0;
	for (p_dat = p_start; p_dat < p_end && i<num_samples; p_dat += p_inc) {
		// 12-bit sample needs to be MSB aligned
		// https://wiki.analog.com/resources/eval/user-guides/ad-fmcomms2-ebz/software/basic_iq_datafiles#binary_format
        ((int16_t*)p_dat)[0] = (int16_t)(8196.0*creal(buf_tx[i])); // Real (I)
        ((int16_t*)p_dat)[1] = (int16_t)(8196.0*cimag(buf_tx[i++])); // Imag (Q)

		// data check
        if (creal(buf_tx[i-1])>=4 || creal(buf_tx[i-1])<=-4
                || cimag(buf_tx[i-1])>=4 || cimag(buf_tx[i-1])<=-4) {
			LOG(WARN,"[Platform] Clipping in TX buffer!\n");
		}
	}
	return i;
}

// Flushes the TX buffer and transfers data to Kernel buffer
// so that samples will be sent
int pluto_transmit(platform hw)
{
    pluto_data pluto = (pluto_data)hw->data;
    ssize_t nbytes_tx;
	// Schedule TX buffer
	nbytes_tx = iio_buffer_push(pluto->txbuf);
	if (nbytes_tx < 0) { printf("Error pushing buf %d\n", (int) nbytes_tx); }
	return nbytes_tx;
}


// Receive samples from Kernel buffer
// returns the number of received samples
int pluto_receive(platform hw, float complex* buf_rx)
{
    pluto_data pluto = (pluto_data)hw->data;
    ssize_t nbytes_rx;
	char *p_dat, *p_start, *p_end;
	ptrdiff_t p_inc;

	// Refill RX buffer
	nbytes_rx = iio_buffer_refill(pluto->rxbuf);
	if (nbytes_rx < 0) { printf("Error refilling buf %d\n",(int) nbytes_rx); }

	// READ: Get pointers to RX buf and read IQ from RX buf port 0
	p_start = iio_buffer_first(pluto->rxbuf, pluto->rx0_i);
	p_inc = iio_buffer_step(pluto->rxbuf);
	p_end = iio_buffer_end(pluto->rxbuf);

	uint i=0;
	for (p_dat = p_start; p_dat < p_end; p_dat += p_inc) {
		buf_rx[i++] = ((int16_t*)p_dat)[0]/2048.0 + I*((int16_t*)p_dat)[1]/2048.0;
	}
	return i;
}

platform init_generic(platform hw, uint buf_len)
{
    pluto_data pluto = (pluto_data)hw->data;

    // RX stream config
    pluto->rxcfg.bw_hz = 1703632;   // Analog filter corner freq. Calculated by matlab filter tool
    pluto->rxcfg.fs_hz = KHZ(256*8);   // 8*256khz, decimation is done after this stage
    pluto->rxcfg.lo_hz = KHZ(430000); // 430Mhz rf frequency
    pluto->rxcfg.rfport = "A_BALANCED"; // port A (select for rf freq.)

	// TX stream config
    pluto->txcfg.bw_hz = 1701126; // Analog filter corner freq: 1.6*fs
    pluto->txcfg.fs_hz = KHZ(256*8);   // 2.5 MS/s tx sample rate
    pluto->txcfg.lo_hz = MHZ(430); // 430Mhz rf frequency
    pluto->txcfg.rfport = "A"; // port A (select for rf freq.)

    pluto->ad9361_phy = get_ad9361_phy(pluto->ctx);

	printf("* Acquiring AD9361 streaming devices\n");
	ASSERT(get_ad9361_stream_dev(pluto->ctx, TX, &pluto->tx) && "No tx dev found");
	ASSERT(get_ad9361_stream_dev(pluto->ctx, RX, &pluto->rx) && "No rx dev found");

	//enable fir filters on both channel
	printf("* Enabling FIR filter on phy channels\n");
	struct iio_channel* chn;
	get_phy_chan(pluto->ctx, RX, 0, &chn);
	wr_ch_lli(chn, "filter_fir_en", 1);
	get_phy_chan(pluto->ctx, TX, 0, &chn);
	wr_ch_lli(chn, "filter_fir_en", 1);

	// set buffer size
	printf("* Configure kernel buffer count for TXRX\n");
	if(iio_device_set_kernel_buffers_count(pluto->tx,KERNEL_BUF_TX)!=0) {
		printf("Error configuring kernel buffer count for TX!\n");
	}
	if(iio_device_set_kernel_buffers_count(pluto->rx,KERNEL_BUF_RX)!=0) {
		printf("Error configuring kernel buffer count for RX!\n");
	}

	printf("* Configuring AD9361 for streaming\n");
	ASSERT(cfg_ad9361_streaming_ch(pluto->ctx, &pluto->rxcfg, RX, 0) && "RX port 0 not found");
	ASSERT(cfg_ad9361_streaming_ch(pluto->ctx, &pluto->txcfg, TX, 0) && "TX port 0 not found");

	printf("* Initializing AD9361 IIO streaming channels\n");
	ASSERT(get_ad9361_stream_ch(pluto->ctx, RX, pluto->rx, 0, &pluto->rx0_i) && "RX chan i not found");
	ASSERT(get_ad9361_stream_ch(pluto->ctx, RX, pluto->rx, 1, &pluto->rx0_q) && "RX chan q not found");
	ASSERT(get_ad9361_stream_ch(pluto->ctx, TX, pluto->tx, 0, &pluto->tx0_i) && "TX chan i not found");
	ASSERT(get_ad9361_stream_ch(pluto->ctx, TX, pluto->tx, 1, &pluto->tx0_q) && "TX chan q not found");

	// Enable dec/int stage of cf-ad9361-lpc / cf-ad9361-dds-core-lpc
	wr_ch_lli(pluto->rx0_i,"sampling_frequency",pluto->rxcfg.fs_hz/8);
	wr_ch_lli(pluto->tx0_i,"sampling_frequency",pluto->txcfg.fs_hz/8);

	printf("* Set TX gain\n");
    // Set TX gain
	get_phy_chan(pluto->ctx, TX, 0, &chn);
    wr_ch_lli(chn, "hardwaregain", 0.0);

    // Set RX AGC to slow attack
    get_phy_chan(pluto->ctx, RX, 0, &chn);
    wr_ch_str(chn, "gain_control_mode", "slow_attack"); // fast_attack, slow_attack, manual
    //wr_ch_lli(chn, "hardwaregain", 26.0);

	printf("* Enabling IIO streaming channels\n");
	iio_channel_enable(pluto->rx0_i);
	iio_channel_enable(pluto->rx0_q);
	iio_channel_enable(pluto->tx0_i);
	iio_channel_enable(pluto->tx0_q);


	printf("* Creating non-cyclic IIO buffers\n");
	pluto->rxbuf = iio_device_create_buffer(pluto->rx, buf_len, false);
	if (!pluto->rxbuf) {
		perror("Could not create RX buffer");
		shutdown(hw);
	}
    pluto->txbuf = iio_device_create_buffer(pluto->tx, buf_len, false);
	if (!pluto->txbuf) {
		perror("Could not create TX buffer");
		shutdown(hw);
	}

	// Generate platform interface
	hw->platform_rx = pluto_receive;
	hw->platform_tx_prep = pluto_prep_tx;
	hw->platform_tx_push = pluto_transmit;
	hw->end = shutdown;
}

long long pluto_get_rxgain(platform hw)
{
    pluto_data pluto = (pluto_data)hw->data;
    long long gain = 0;
    struct iio_channel* chn;

    get_phy_chan(pluto->ctx, RX, 0, &chn);
    iio_channel_attr_read_longlong(chn, "hardwaregain", &gain);
    return gain;
}

void pluto_set_rxgain(platform hw, int gain)
{
    pluto_data pluto = (pluto_data)hw->data;
    struct iio_channel* chn;

    get_phy_chan(pluto->ctx, RX, 0, &chn);
    wr_ch_str(chn, "gain_control_mode", "manual");
    wr_ch_lli(chn, "hardwaregain", gain);
}

// set TX gain in dBm
void pluto_set_txgain(platform hw, int gain)
{
    pluto_data pluto = (pluto_data)hw->data;
    struct iio_channel* chn;

    get_phy_chan(pluto->ctx, TX, 0, &chn);
    wr_ch_lli(chn, "hardwaregain", gain);
}

int pluto_set_tx_freq(platform hw, long long txfreq)
{
    pluto_data pluto = (pluto_data)hw->data;
    struct iio_channel* chn;

	if (!get_lo_chan(pluto->ctx, TX, &chn)) { return false; }
	wr_ch_lli(chn, "frequency", txfreq);
	return true;
}

int pluto_set_rx_freq(platform hw, long long rxfreq)
{
    pluto_data pluto = (pluto_data)hw->data;
    struct iio_channel* chn;

	if (!get_lo_chan(pluto->ctx, RX, &chn)) { return false; }
	wr_ch_lli(chn, "frequency", rxfreq);
	return true;
}

// Set the AD9361 to TDD or FDD mode
int pluto_set_duplex_mode(platform hw, enum duplex_type mode)
{
    pluto_data pluto = (pluto_data)hw->data;
    struct iio_device* dev = get_ad9361_phy(pluto->ctx);
    if (dev==NULL)
        return -1;

    if (mode==FDD) {
        iio_device_debug_attr_write_bool(dev, "adi,frequency-division-duplex-mode-enable", 1);
        iio_device_debug_attr_write_bool(dev, "initialize", 1);
    } else {
        iio_device_debug_attr_write_bool(dev, "adi,frequency-division-duplex-mode-enable", 0);
        iio_device_debug_attr_write_bool(dev, "adi,gpo0-slave-rx-enable", 1);
        iio_device_debug_attr_write_bool(dev, "adi,gpo1-slave-tx-enable", 1);
        iio_device_debug_attr_write_bool(dev, "initialize", 1);
    }
    return 0;
}

void pluto_tdd_set_tx(platform hw)
{
    pluto_data pluto = (pluto_data)hw->data;
    iio_device_attr_write(pluto->ad9361_phy, "ensm_mode", "tx");
}

void pluto_tdd_set_rx(platform hw)
{
    pluto_data pluto = (pluto_data)hw->data;
    iio_device_attr_write(pluto->ad9361_phy, "ensm_mode", "rx");
}

void pluto_tdd_set_switch_delay(platform hw, int delay_us)
{
    pluto_data pluto = (pluto_data)hw->data;
    iio_device_attr_write_longlong(pluto->ad9361_phy, "gpo0-rx-delay-us", delay_us);
    iio_device_attr_write_longlong(pluto->ad9361_phy, "gpo0-tx-delay-us", delay_us);
    pluto_set_duplex_mode(hw,TDD);
}

// Initialize a pluto network context
platform init_pluto_network_platform(uint buf_len)
{
    platform pluto = malloc(sizeof(struct platform_s));
    pluto_data data = calloc(sizeof(struct pluto_data_s),1);
    pluto->data = data;
    char ipaddr[] = "192.168.4.1";
	printf("* Acquiring IIO Device at %s\n",ipaddr);
	ASSERT((data->ctx = iio_create_network_context(ipaddr)) && "No context");
	ASSERT(iio_context_get_devices_count(data->ctx) > 0 && "No devices");

	init_generic(pluto, buf_len);
	return pluto;
}

// Initialize a local context. Used for buidling directly
// on the pluto
platform init_pluto_platform(uint buf_len)
{
    platform pluto = malloc(sizeof(struct platform_s));
    pluto_data data = calloc(sizeof(struct pluto_data_s),1);
    pluto->data = data;
	printf("* Acquiring IIO context\n");
	ASSERT((data->ctx = iio_create_local_context()) && "No context");
	ASSERT(iio_context_get_devices_count(data->ctx) > 0 && "No devices");

	init_generic(pluto, buf_len);
	return pluto;
}

// Thread monitors the TX and RX buffer for overflow/underflows
// adapted example from AD libiio:
// https://github.com/analogdevicesinc/libiio/blob/master/tests/iio_adi_xflow_check.c
static void *monitor_thread_fn(void *hw)
{
    pluto_data pluto = ((platform)hw)->data;
	uint32_t rxval, txval;
	int ret;

	/* Give the main thread a moment to start the DMA */
	sleep(1);

	/* Clear all status bits for TX and RX dev*/
	iio_device_reg_write(pluto->tx, 0x80000088, 0x6);
	iio_device_reg_write(pluto->rx, 0x80000088, 0x6);

	while (1) {
		// Check TX device
		ret = iio_device_reg_read(pluto->tx, 0x80000088, &txval);
		if (ret) {
			printf("Monitor: Failed to read status register: %s\n",
					strerror(-ret));
		} else if (txval & 1)
			printf("Monitor: TX DEVICE UNDERFLOW DETECTED!\n");

		// Check RX device
		ret = iio_device_reg_read(pluto->rx, 0x80000088, &rxval);
		if (ret) {
			printf("Monitor: Failed to read status register: %s\n",
					strerror(-ret));
		} else if (rxval & 4)
			printf("Monitor: RX DEVICE OVERFLOW DETECTED!\n");

		/* Clear bits */
		if (txval)
			iio_device_reg_write(pluto->tx, 0x80000088, txval);
		if (rxval)
			iio_device_reg_write(pluto->rx, 0x80000088, rxval);
		sleep(1);
	}

	return (void *)0;
}

// Start the monitoring thread
pthread_t pluto_start_monitor(platform hw)
{
	pthread_t monitor_thread;

	int ret = pthread_create(&monitor_thread, NULL, monitor_thread_fn, hw);
	if (ret)
		printf("Failed to create buffer monitor thread\n");
	return monitor_thread;
}
