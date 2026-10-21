/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * GY-85 module: ADXL345 accelerometer and ITG3205 gyro on I2C0. The
 * accelerometer's data-ready interrupt times each sample; the gyro is read
 * right after it. While streaming is on, samples are queued for the link.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "health.h"
#include "imu.h"

LOG_MODULE_REGISTER(imu, LOG_LEVEL_INF);

#define USER_NODE DT_PATH(zephyr_user)

#define ADXL345_ADDR        0x53
#define ADXL345_DEVID       0x00
#define ADXL345_DEVID_VAL   0xE5
#define ADXL345_BW_RATE     0x2C
#define ADXL345_POWER_CTL   0x2D
#define ADXL345_INT_ENABLE  0x2E
#define ADXL345_INT_MAP     0x2F
#define ADXL345_DATA_FORMAT 0x31
#define ADXL345_DATAX0      0x32
#define ADXL345_FIFO_CTL    0x38
#define ADXL345_MEASURE     BIT(3)
#define ADXL345_DATA_READY  BIT(7)
/* Full resolution (3.9 mg/LSB at every range), +-16 g, active-high INT. */
#define ADXL345_FORMAT_16G  (BIT(3) | 0x03)

#define ITG3205_ADDR_LOW    0x68
#define ITG3205_ADDR_HIGH   0x69
#define ITG3205_WHO_AM_I    0x00
#define ITG3205_SMPLRT_DIV  0x15
#define ITG3205_DLPF_FS     0x16
#define ITG3205_GYRO_XOUT_H 0x1D
#define ITG3205_PWR_MGM     0x3E
/* +-2000 deg/s, 188 Hz low-pass, 1 kHz internal rate. */
#define ITG3205_FS_2000_188 0x19
/* Clock from the X gyro, more stable than the internal oscillator. */
#define ITG3205_CLK_PLL_X   0x01

#define SAMPLE_STACK_SIZE   1536
#define SAMPLE_PRIORITY     K_PRIO_PREEMPT(2)

#define RING_SAMPLES        128

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static const struct gpio_dt_spec drdy = GPIO_DT_SPEC_GET(USER_NODE, imu_int_gpios);
static struct gpio_callback drdy_cb;
static K_SEM_DEFINE(drdy_sem, 0, 1);
static volatile uint32_t drdy_us;

static uint8_t gyro_addr;
static uint32_t period_us;

#define MG_TO_LSB(mg)  (((mg) * 1000) / IMU_ACCEL_UG_PER_LSB)
#define MDPS_TO_Q4(md) (((md) * 16 * IMU_GYRO_LSB_PER_DPS_X1000) / 1000000)

/* Zero-g readings of the accelerometer in its own axes. */
static const int16_t accel_ofs[3] = {
	MG_TO_LSB(CONFIG_ROBOT_IMU_ACCEL_OFS_X),
	MG_TO_LSB(CONFIG_ROBOT_IMU_ACCEL_OFS_Y),
	MG_TO_LSB(CONFIG_ROBOT_IMU_ACCEL_OFS_Z),
};

/*
 * Gyro bias in the gyro's own axes, raw units times 16. It drifts with
 * temperature, so it is measured again whenever the robot stands still.
 */
static int32_t gyro_bias_q4[3] = {
	MDPS_TO_Q4(CONFIG_ROBOT_IMU_GYRO_BIAS_X),
	MDPS_TO_Q4(CONFIG_ROBOT_IMU_GYRO_BIAS_Y),
	MDPS_TO_Q4(CONFIG_ROBOT_IMU_GYRO_BIAS_Z),
};

/* Bias measurement: motors stopped this long, then this many samples. */
#define BIAS_IDLE_SAMPLES  CONFIG_ROBOT_IMU_RATE
#define BIAS_AVG_SAMPLES   512
/* A rate further than this from the bias means the robot is moved by hand. */
#define BIAS_MAX_DEV_Q4    MDPS_TO_Q4(3000)

static uint32_t idle_samples;
static uint32_t bias_n;
static int32_t bias_sum[3];
static uint32_t bias_updates;

/* Shared between the sampling thread and the link. */
static struct k_spinlock lock;
static struct imu_stats stats;
static struct imu_sample last_sample;
static struct imu_sample ring[RING_SAMPLES];
static size_t ring_head;
static size_t ring_count;
static bool streaming;

static K_SEM_DEFINE(started_sem, 0, 1);

static int reg_write(uint8_t addr, uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte(i2c, addr, reg, val);
}

static uint8_t rate_code(uint32_t hz)
{
	/* ADXL345 BW_RATE codes: 100 Hz is 0x0A, each step doubles. */
	switch (hz) {
	case 100U:
		return 0x0A;
	case 200U:
		return 0x0B;
	case 400U:
		return 0x0C;
	default:
		return 0x0D;
	}
}

static int accel_setup(void)
{
	uint8_t id = 0U;
	int err;

	err = i2c_reg_read_byte(i2c, ADXL345_ADDR, ADXL345_DEVID, &id);
	if ((err != 0) || (id != ADXL345_DEVID_VAL)) {
		LOG_DBG("ADXL345 not found at 0x%02x (err %d, id 0x%02x)", ADXL345_ADDR, err, id);
		return -ENODEV;
	}

	err = reg_write(ADXL345_ADDR, ADXL345_POWER_CTL, 0U);
	err = (err == 0) ? reg_write(ADXL345_ADDR, ADXL345_DATA_FORMAT, ADXL345_FORMAT_16G) : err;
	err = (err == 0) ? reg_write(ADXL345_ADDR, ADXL345_BW_RATE,
				     rate_code(CONFIG_ROBOT_IMU_RATE)) : err;
	err = (err == 0) ? reg_write(ADXL345_ADDR, ADXL345_FIFO_CTL, 0U) : err;
	/* Data ready on INT1; INT_MAP bits set would route to INT2. */
	err = (err == 0) ? reg_write(ADXL345_ADDR, ADXL345_INT_MAP,
				     IS_ENABLED(CONFIG_ROBOT_IMU_INT2) ? ADXL345_DATA_READY : 0U)
			 : err;
	err = (err == 0) ? reg_write(ADXL345_ADDR, ADXL345_INT_ENABLE, ADXL345_DATA_READY) : err;
	err = (err == 0) ? reg_write(ADXL345_ADDR, ADXL345_POWER_CTL, ADXL345_MEASURE) : err;

	return err;
}

static void gyro_setup(void)
{
	static const uint8_t addrs[] = {ITG3205_ADDR_LOW, ITG3205_ADDR_HIGH};

	for (size_t i = 0U; i < ARRAY_SIZE(addrs); i++) {
		uint8_t id = 0U;

		if ((i2c_reg_read_byte(i2c, addrs[i], ITG3205_WHO_AM_I, &id) == 0) &&
		    ((id & 0x7EU) == ITG3205_ADDR_LOW)) {
			gyro_addr = addrs[i];
			break;
		}
	}

	if (gyro_addr == 0U) {
		LOG_WRN("ITG3205 gyro not found, streaming acceleration only");
		return;
	}

	if ((reg_write(gyro_addr, ITG3205_PWR_MGM, ITG3205_CLK_PLL_X) != 0) ||
	    (reg_write(gyro_addr, ITG3205_DLPF_FS, ITG3205_FS_2000_188) != 0) ||
	    (reg_write(gyro_addr, ITG3205_SMPLRT_DIV, 0U) != 0)) {
		LOG_WRN("ITG3205 setup failed, streaming acceleration only");
		gyro_addr = 0U;
	}
}

static void drdy_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/* Level interrupt: mask it until the sample is read. */
	(void)gpio_pin_interrupt_configure_dt(&drdy, GPIO_INT_DISABLE);
	drdy_us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_64());
	k_sem_give(&drdy_sem);
}

/*
 * Average the raw gyro while the motors have been stopped for a while and
 * nothing turns the robot; the mean becomes the new bias.
 */
static void track_bias(const int16_t raw[3], const struct drive_snapshot *d)
{
	if ((d->left != 0) || (d->right != 0)) {
		idle_samples = 0U;
		bias_n = 0U;
		return;
	}

	if (idle_samples < BIAS_IDLE_SAMPLES) {
		idle_samples++;
		return;
	}

	for (size_t i = 0U; i < 3U; i++) {
		int32_t dev = ((int32_t)raw[i] * 16) - gyro_bias_q4[i];

		if ((dev > BIAS_MAX_DEV_Q4) || (dev < -BIAS_MAX_DEV_Q4)) {
			bias_n = 0U;
			return;
		}
	}

	if (bias_n == 0U) {
		bias_sum[0] = 0;
		bias_sum[1] = 0;
		bias_sum[2] = 0;
	}

	for (size_t i = 0U; i < 3U; i++) {
		bias_sum[i] += raw[i];
	}

	if (++bias_n == BIAS_AVG_SAMPLES) {
		for (size_t i = 0U; i < 3U; i++) {
			gyro_bias_q4[i] = (bias_sum[i] * 16) / (int32_t)BIAS_AVG_SAMPLES;
		}
		bias_n = 0U;
		bias_updates++;
	}
}

static int16_t gyro_corrected(int16_t raw, int32_t bias_q4)
{
	/* Round the bias to whole raw units. */
	return (int16_t)(raw - ((bias_q4 + ((bias_q4 >= 0) ? 8 : -8)) / 16));
}

static int read_sample(struct imu_sample *s)
{
	uint8_t buf[6];
	int16_t a[3];
	int16_t g[3] = {0};
	int err;

	/* Reading all six data bytes clears the accelerometer's data ready. */
	err = i2c_burst_read(i2c, ADXL345_ADDR, ADXL345_DATAX0, buf, sizeof(buf));
	if (err != 0) {
		return err;
	}

	for (size_t i = 0U; i < 3U; i++) {
		a[i] = (int16_t)(sys_get_le16(&buf[2U * i]) - accel_ofs[i]);
	}

	if (gyro_addr != 0U) {
		err = i2c_burst_read(i2c, gyro_addr, ITG3205_GYRO_XOUT_H, buf, sizeof(buf));
		if (err != 0) {
			return err;
		}
		for (size_t i = 0U; i < 3U; i++) {
			g[i] = (int16_t)sys_get_be16(&buf[2U * i]);
		}
	}

	drive_snapshot_get(&s->drive);

	if (gyro_addr != 0U) {
		track_bias(g, &s->drive);
		for (size_t i = 0U; i < 3U; i++) {
			g[i] = gyro_corrected(g[i], gyro_bias_q4[i]);
		}
	}

	/*
	 * On this robot both chips have x forward, y right and z down. Turn
	 * that into x forward, y left, z up.
	 */
	s->accel[0] = a[0];
	s->accel[1] = (int16_t)-a[1];
	s->accel[2] = (int16_t)-a[2];
	s->gyro[0] = g[0];
	s->gyro[1] = (int16_t)-g[1];
	s->gyro[2] = (int16_t)-g[2];

	return 0;
}

static void store(const struct imu_sample *s, bool timed_out)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	stats.samples++;
	stats.bias_updates = bias_updates;
	memcpy(stats.gyro_bias_q4, gyro_bias_q4, sizeof(stats.gyro_bias_q4));
	if (timed_out) {
		stats.int_timeouts++;
	}
	last_sample = *s;

	if (streaming) {
		if (ring_count < RING_SAMPLES) {
			ring[(ring_head + ring_count) % RING_SAMPLES] = *s;
			ring_count++;
		} else {
			stats.dropped++;
		}
	}

	k_spin_unlock(&lock, key);
}

static void sample_thread(void *p1, void *p2, void *p3)
{
	struct imu_sample s = {0};

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	(void)k_sem_take(&started_sem, K_FOREVER);

	for (;;) {
		/* Two periods without an interrupt: read anyway and count it. */
		bool timed_out = k_sem_take(&drdy_sem, K_USEC(2U * period_us)) != 0;

		health_beat(HEALTH_IMU);
		s.t_us = timed_out ? (uint32_t)k_cyc_to_us_floor64(k_cycle_get_64()) : drdy_us;

		if (read_sample(&s) == 0) {
			store(&s, timed_out);
		}

		(void)gpio_pin_interrupt_configure_dt(&drdy, GPIO_INT_LEVEL_ACTIVE);
	}
}

K_THREAD_DEFINE(imu_sample_tid, SAMPLE_STACK_SIZE, sample_thread, NULL, NULL, NULL,
		SAMPLE_PRIORITY, 0, 0);

size_t imu_stream_take(struct imu_sample *out, size_t max)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	size_t n = MIN(ring_count, max);

	for (size_t i = 0U; i < n; i++) {
		out[i] = ring[(ring_head + i) % RING_SAMPLES];
	}
	ring_head = (ring_head + n) % RING_SAMPLES;
	ring_count -= n;

	k_spin_unlock(&lock, key);

	return n;
}

size_t imu_stream_pending(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	size_t n = ring_count;

	k_spin_unlock(&lock, key);

	return n;
}

void imu_stream_enable(bool on)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	streaming = on;
	ring_head = 0U;
	ring_count = 0U;

	k_spin_unlock(&lock, key);
}

/* Set up both chips and the interrupt; 0 when sampling can start. */
static int imu_setup(void)
{
	int err;

	err = accel_setup();
	if (err != 0) {
		return -ENODEV;
	}

	gyro_setup();

	err = gpio_pin_configure_dt(&drdy, GPIO_INPUT);
	if (err == 0) {
		gpio_init_callback(&drdy_cb, drdy_handler, BIT(drdy.pin));
		err = gpio_add_callback_dt(&drdy, &drdy_cb);
	}
	if (err == 0) {
		err = gpio_pin_interrupt_configure_dt(&drdy, GPIO_INT_LEVEL_ACTIVE);
	}
	if (err != 0) {
		LOG_ERR("Interrupt setup failed (%d)", err);
		return err;
	}

	k_spinlock_key_t key = k_spin_lock(&lock);

	stats.rate_hz = CONFIG_ROBOT_IMU_RATE;
	stats.gyro_addr = gyro_addr;
	stats.accel_ok = true;
	k_spin_unlock(&lock, key);

	LOG_INF("ADXL345 ok, gyro %s0x%02x, %u Hz", (gyro_addr != 0U) ? "" : "missing ",
		gyro_addr, CONFIG_ROBOT_IMU_RATE);

	health_watch(HEALTH_IMU, true);
	k_sem_give(&started_sem);

	return 0;
}

/* The module may be powered or connected after boot: keep looking. */
#define RETRY_PERIOD K_SECONDS(2)

static void retry_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(retry_work, retry_work_handler);

static void retry_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (imu_setup() != 0) {
		(void)k_work_schedule(&retry_work, RETRY_PERIOD);
	}
}

int imu_start(void)
{
	period_us = USEC_PER_SEC / CONFIG_ROBOT_IMU_RATE;

	if (!device_is_ready(i2c) || !gpio_is_ready_dt(&drdy)) {
		LOG_ERR("I2C or interrupt GPIO not ready");
		return -ENODEV;
	}

	if (imu_setup() != 0) {
		LOG_WRN("IMU not found, retrying every 2 s");
		(void)k_work_schedule(&retry_work, RETRY_PERIOD);
		return -ENODEV;
	}

	return 0;
}

void imu_get(struct imu_stats *out, struct imu_sample *last)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	*out = stats;
	if (last != NULL) {
		*last = last_sample;
	}

	k_spin_unlock(&lock, key);
}
