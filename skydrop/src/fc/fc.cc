#include "fc.h"

#include "../drivers/sensors/devices.h"
#include "../drivers/uart.h"

#include "vario.h"
#include "audio.h"

volatile flight_data_t fc;

Timer fc_meas_timer;

void fc_init()
{
	DEBUG(" *** Flight computer init ***\n");

	//load configuration
	DEBUG("Loading data form EEPROM\n");
	eeprom_busy_wait();

	fc.QNH1 = eeprom_read_float(&config.altitude.QNH1);
	DEBUG("QNH1 %0.1f\n", fc.QNH1);

	fc.QNH2 = eeprom_read_float(&config.altitude.QNH2);
	DEBUG("QNH2 %0.1f\n", fc.QNH2);

	fc.digital_vario_dampening = eeprom_read_float(&config.vario.digital_vario_dampening);
	if (fc.digital_vario_dampening == 0)
		fc.digital_vario_dampening = 1;
	else
		fc.digital_vario_dampening = 1.0 / 100.0 / fc.digital_vario_dampening;

	DEBUG("digital_vario_dampening %0.2f\n", fc.digital_vario_dampening);

	fc.avg_vario_dampening = eeprom_read_float(&config.vario.avg_vario_dampening);
	if (fc.avg_vario_dampening == 0)
		fc.avg_vario_dampening = 1;
	else
		fc.avg_vario_dampening = 1.0 / 100.0 / fc.avg_vario_dampening;

	DEBUG("avg_vario_dampening %0.2f\n", fc.avg_vario_dampening);


	for (uint8_t i=0; i<NUMBER_OF_ALTIMETERS; i++)
	{
		DEBUG("altimeter[%d]\n", i);
		fc.altimeter[i].altitude = 0;

		fc.altimeter[i].flags = eeprom_read_byte(&config.altitude.altimeter[i].flags);
		DEBUG(" flags %02X\n", fc.altimeter[i].flags);

		eeprom_read_block((int16_t *) &fc.altimeter[i].delta, &config.altitude.altimeter[i].delta, sizeof(int16_t));
		DEBUG(" delta %d\n", fc.altimeter[i].delta);
	}

	eeprom_read_block((void *)&fc.mag_bias, &config.calibration.mag_bias, sizeof(vector_i16_t));
	DEBUG("mag_bias\n");
	DEBUG(" x %d\n", fc.mag_bias.x);
	DEBUG(" y %d\n", fc.mag_bias.y);
	DEBUG(" z %d\n", fc.mag_bias.z);

	eeprom_read_block((void *)&fc.mag_sensitivity, &config.calibration.mag_sensitivity, sizeof(vector_i16_t));
	DEBUG("mag_sensitivity\n");
	DEBUG(" x %d\n", fc.mag_sensitivity.x);
	DEBUG(" y %d\n", fc.mag_sensitivity.y);
	DEBUG(" z %d\n", fc.mag_sensitivity.z);

	eeprom_read_block((void *)&fc.acc_bias, &config.calibration.acc_bias, sizeof(vector_i16_t));
	DEBUG("acc_bias\n");
	DEBUG(" x %d\n", fc.acc_bias.x);
	DEBUG(" y %d\n", fc.acc_bias.y);
	DEBUG(" z %d\n", fc.acc_bias.z);

	eeprom_read_block((void *)&fc.acc_sensitivity, &config.calibration.acc_sensitivity, sizeof(vector_i16_t));
	DEBUG("acc_sensitivity\n");
	DEBUG(" x %d\n", fc.acc_sensitivity.x);
	DEBUG(" y %d\n", fc.acc_sensitivity.y);
	DEBUG(" z %d\n", fc.acc_sensitivity.z);

	eeprom_read_block((void *)&fc.buzzer_freq, &config.audio_profile.freq, sizeof(int16_t) * AUDIO_PROFILE_SIZE);
	DEBUG("buzzer_freq\n");
	for (uint8_t i = 0; i < AUDIO_PROFILE_SIZE; i++)
		DEBUG(" %d", fc.buzzer_freq[i]);
	DEBUG("\n");

	eeprom_read_block((void *)&fc.buzzer_length, &config.audio_profile.length, sizeof(int16_t) * AUDIO_PROFILE_SIZE);
	DEBUG("buzzer_length\n");
	for (uint8_t i = 0; i < AUDIO_PROFILE_SIZE; i++)
		DEBUG(" %d", fc.buzzer_length[i]);
	DEBUG("\n");

	eeprom_read_block((void *)&fc.buzzer_pause, &config.audio_profile.pause, sizeof(int16_t) * AUDIO_PROFILE_SIZE);
	DEBUG("buzzer_pause\n");
	for (uint8_t i = 0; i < AUDIO_PROFILE_SIZE; i++)
		DEBUG(" %d", fc.buzzer_pause[i]);
	DEBUG("\n");

	fc.audio_lift = eeprom_read_word((uint16_t *)&config.audio_profile.lift);
	DEBUG("audio_lift %d\n", fc.audio_lift);

	fc.audio_sink = eeprom_read_word((uint16_t *)&config.audio_profile.sink);
	DEBUG("audio_sink %d\n", fc.audio_sink);

	fc.audio_fluid = eeprom_read_byte(&config.audio_profile.fluid);
	DEBUG("audio_fluid %d\n", fc.audio_fluid);

	fc.audio_volume = eeprom_read_byte(&config.audio_profile.volume);
	DEBUG("audio_volume %d\n", fc.audio_volume);


	DEBUG("\n");
	//init calculators
	vario_init();
	audio_init();

//	gps_init();
//	bt_init();

	DEBUG("2\n");

	//VCC to baro, acc/mag gyro
	MEMS_POWER_ON;

	GpioSetDirection(IO0, OUTPUT);
	GpioWrite(IO0, HIGH);

	//init and test i2c
	mems_i2c_init();

	DEBUG("3\n");


	//Barometer
	ms5611.Init(&mems_i2c, MS5611_ADDRESS_CSB_LO);

	//Magnetometer + Accelerometer
	lsm303d_settings lsm_cfg;

	lsm_cfg.enabled = true;
	lsm_cfg.accOdr = lsm_acc_1600Hz;
	lsm_cfg.accScale = lsm_acc_16g;

	lsm_cfg.magOdr = lsm_mag_100Hz;
	lsm_cfg.magScale = lsm_mag_4g;
	lsm_cfg.magHiRes = true;

	lsm_cfg.tempEnable = false;

	DEBUG("5\n");

	//Gyro
	l3gd20_settings l3g_cfg;
	l3g_cfg.enabled = true;
	l3g_cfg.bw = l3g_50Hz;
	l3g_cfg.odr = l3g_760Hz;
	l3g_cfg.scale = l3g_2000dps;

	sht21_settings sht_cfg;
	sht_cfg.rh_enabled = true;
	sht_cfg.temp_enabled = true;

	DEBUG("6\n");

	//XXX: do self-test?
	lsm303d.Init(&mems_i2c, lsm_cfg);
	lsm303d.Start();

	l3gd20.Init(&mems_i2c, l3g_cfg);
	l3gd20.Start();

	sht21.Init(&mems_i2c, sht_cfg);

	DEBUG("7\n");

	//Measurement timer
	FC_MEAS_TIMER_PWR_ON;

	fc_meas_timer.Init(FC_MEAS_TIMER, timer_div256); //125 == 1ms
	fc_meas_timer.SetInterruptPriority(MEDIUM);
	fc_meas_timer.EnableInterrupts(timer_overflow | timer_compareA | timer_compareB | timer_compareC);
	fc_meas_timer.SetTop(125 * 10); // == 10ms
	fc_meas_timer.SetCompare(timer_A, 100); // == 0.64ms
	fc_meas_timer.SetCompare(timer_B, 430); // == 2.7ms
	fc_meas_timer.SetCompare(timer_C, 555); // == 3.7ms
	fc_meas_timer.Start();

	DEBUG(" *** FC init done ***\n");

}

void fc_pause()
{
	fc_meas_timer.Stop();
}

void fc_continue()
{
	fc_meas_timer.Start();
}


ISR(FC_MEAS_TIMER_OVF)
{
	IO1_HIGH
	ms5611.ReadPressure();
	ms5611.StartTemperature();
	lsm303d.StartReadMag(); //it takes 152us to transfer

	ms5611.CompensatePressure();
	IO1_LOW

}


ISR(FC_MEAS_TIMER_CMPA)
{
	IO1_HIGH
	lsm303d.ReadMag(&fc.mag_data.x, &fc.mag_data.y, &fc.mag_data.z);
	ms5611.ReadTemperature();
	ms5611.StartPressure();
	lsm303d.StartReadAccStream(16); //it take 1600us to transfer

	vario_calc(ms5611.pressure);
	if (fc.baro_valid)
		audio_step(fc.vario);

	ms5611.CompensateTemperature();

	IO1_LOW
}

ISR(FC_MEAS_TIMER_CMPB)
{
	IO1_HIGH
	lsm303d.ReadAccStreamAvg(&fc.acc_data.x, &fc.acc_data.y, &fc.acc_data.z, 16);
	l3gd20.StartReadGyroStream(7); //it take 1000us to transfer
	IO1_LOW
}

ISR(FC_MEAS_TIMER_CMPC)
{
	IO1_HIGH
	l3gd20.ReadGyroStreamAvg(&fc.gyro_data.x, &fc.gyro_data.y, &fc.gyro_data.z, 7); //it take 1000us to transfer

	IO1_LOW
}


void fc_step()
{
	gps_step();
	bt_step();
}

float fc_alt_to_qnh(float alt, float pressure)
{
	return pressure / pow(1.0 - (alt / 44330.0), 5.255);
}

float fc_press_to_alt(float pressure, float qnh)
{
	return 44330.0 * (1 - pow((pressure / qnh), 0.190295));
}

float fc_alt_to_press(float alt, float qnh)
{
	return qnh * pow(1.0 - (alt / 44330.0), 5.255);
}
