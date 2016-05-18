#include "common.h"
#include "drivers/uart.h"
#include "drivers/storage/storage.h"
#include <string.h>
#include "gui/gui.h"
#include "fc/conf.h"


struct app_info ee_fw_info __attribute__ ((section(".fw_info")));
struct app_info fw_info;

uint8_t hw_revision = HW_REW_UNKNOWN;

extern uint8_t system_rst;

void print_reset_reason()
{
	//Print reset reason
	DEBUG("Reset reason ... ");

	if (system_rst & 0b00100000)
		DEBUG("Software ");
	else
	if (system_rst & 0b00010000)
		DEBUG("Programming ");
	else
	if (system_rst & 0b00001000)
		DEBUG("Watchdog ");
	else
	if (system_rst & 0b00000100)
		DEBUG("Brownout ");
	else
	if (system_rst & 0b00000010)
		DEBUG("External ");
	else
	if (system_rst & 0b00000001)
		DEBUG("Power On ");
	else
		DEBUG("Unknown: %02X", system_rst);

	DEBUG("\n");
}


void print_fw_info()
{
	eeprom_busy_wait();
	eeprom_read_block(&fw_info, &ee_fw_info, sizeof(fw_info));

	DEBUG("App name: ");
	for (uint8_t i = 0; i < APP_INFO_NAME_len; i++)
	{
		uint8_t c = fw_info.app_name[i];
		if (c < 32 || c > 126)
			c = '_';
		DEBUG("%c", c);
	}
	DEBUG("\n");
}

void test_memory()
{
	DEBUG("Free RAM now ... %d\n", freeRam());
}

//----------------------------------------------------------

void turnoff_subsystems()
{
	//PORTA
	PR.PRPA = 0b00000111;
	//PORTB
	PR.PRPB = 0b00000111;
	//PORTC
	PR.PRPC = 0b01111111;
	//PORTD
	PR.PRPD = 0b01111111;
	//PORTE
	PR.PRPE = 0b01111111;
	//PORTF
	PR.PRPF = 0b01111111;
	//PRGEN - RTC must stay on
	PR.PRGEN = 0b01011011;
}

//----------------------------------------------------------

DataBuffer::DataBuffer(uint16_t size)
{
	this->size = size;
	this->length = 0;
	this->write_index = 0;
	this->read_index = 0;

	this->data = new uint8_t[size];
	if (this->data == NULL)
	{
		this->size = 0;
	}
}

DataBuffer::~DataBuffer()
{

}

uint16_t DataBuffer::Read(uint16_t len, uint8_t * * data)
{
	(*data) = this->data + this->read_index;

	if (len > this->length)
		len = this->length;

	if (this->read_index + len > this->size)
		len = this->size - this->read_index;

	this->read_index = (this->read_index + len) % this->size;
	this->length -= len;

	return len;
}

bool DataBuffer::Write(uint16_t len, uint8_t * data)
{
	if (this->size - this->length < len)
		return false;

	if (this->write_index + len > this->size)
	{
		uint16_t first_len = this->size - this->write_index;
		memcpy(this->data + this->write_index, data, first_len);
		memcpy(this->data, data + first_len, len - first_len);
	}
	else
	{
		memcpy(this->data + this->write_index, data, len);
	}

	this->write_index = (this->write_index + len) % this->size;
	this->length += len;

	return true;
}

uint16_t DataBuffer::Length()
{
	return this->length;
}

void DataBuffer::Clear()
{
	this->length = 0;
	this->write_index = 0;
	this->read_index = 0;
}

//***************************************************

volatile uint8_t bat_en_mask = 0;

void bat_en_high(uint8_t mask)
{
	bat_en_mask |= mask;
	GpioWrite(BAT_EN, HIGH);
}

void bat_en_low(uint8_t mask)
{
	bat_en_mask &= ~mask;

	if (bat_en_mask == 0)
		GpioWrite(BAT_EN, LOW);
}

//***************************************************

uint8_t device_id[11];

void GetID() //11 b
{
	enum
	{
		LOTNUM0=8,  // Lot Number Byte 0, ASCII
		LOTNUM1,    // Lot Number Byte 1, ASCII
		LOTNUM2,    // Lot Number Byte 2, ASCII
		LOTNUM3,    // Lot Number Byte 3, ASCII
		LOTNUM4,    // Lot Number Byte 4, ASCII
		LOTNUM5,    // Lot Number Byte 5, ASCII
		WAFNUM =16, // Wafer Number
		COORDX0=18, // Wafer Coordinate X Byte 0
		COORDX1,    // Wafer Coordinate X Byte 1
		COORDY0,    // Wafer Coordinate Y Byte 0
		COORDY1,    // Wafer Coordinate Y Byte 1
	};

	NVM.CMD = NVM_CMD_READ_CALIB_ROW_gc;
	device_id[0] = pgm_read_byte(LOTNUM0);
	device_id[1] = pgm_read_byte(LOTNUM1);
	device_id[2] = pgm_read_byte(LOTNUM2);
	device_id[3] = pgm_read_byte(LOTNUM3);
	device_id[4] = pgm_read_byte(LOTNUM4);
	device_id[5] = pgm_read_byte(LOTNUM5);
	device_id[6] = pgm_read_byte(WAFNUM);
	device_id[7] = pgm_read_byte(COORDX0);
	device_id[8] = pgm_read_byte(COORDX1);
	device_id[9] = pgm_read_byte(COORDY0);
	device_id[10] = pgm_read_byte(COORDY1);
	NVM.CMD = NVM_CMD_NO_OPERATION_gc;
}


void GetID_str(char * id) //23 b
{
	uint8_t * b = device_id;

	sprintf_P(id, PSTR("%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X"), b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10]);
}

//***************************************************


bool cmpn(char * s1, const char * s2, uint8_t n)
{
	for (uint8_t i = 0; i < n; i++)
	{
		if (s1[i] != s2[i])
			return false;
	}
	return true;
}

bool cmpn_p(char * s1, const char * s2, uint8_t n)
{
	for (uint8_t i = 0; i < n; i++)
	{
		if (s1[i] != pgm_read_byte(&s2[i]))
			return false;
	}
	return true;
}

int freeRam()
{
	extern int __heap_start, *__brkval;
	int v;
	return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

bool LoadEEPROM()
{
	FILINFO fno;

	if (f_stat("UPDATE.EE", &fno) == FR_OK)
	{
		DEBUG("EE update found.\n");

		FIL * ee_file;
		ee_file = new FIL;

		f_open(ee_file, "UPDATE.EE", FA_READ);
		uint16_t rd = 0;

		byte4 tmp;

		f_read(ee_file, tmp.uint8, sizeof(tmp), &rd);
		if (tmp.uint32 != BUILD_NUMBER)
		{
			gui_showmessage_P(PSTR("UPDATE.EE is not\ncompatibile!"));
			DEBUG("Rejecting update file %lu != %lu\n", tmp.uint32, BUILD_NUMBER);
			delete ee_file;
			return false;
		}

		//rewind the file
		f_lseek(ee_file, 0);

		for (uint16_t i = 0; i < fno.fsize; i += rd)
		{
			uint8_t buf[256];

			f_read(ee_file, buf, sizeof(buf), &rd);

			eeprom_busy_wait();
			eeprom_update_block(buf, (uint8_t *)(APP_INFO_EE_offset + i), rd);
		}

		gui_showmessage_P(PSTR("UPDATE.EE\napplied."));
		delete ee_file;
		return true;
	}
	return false;
}

bool StoreEEPROM()
{
	ewdt_reset();
	DEBUG("Storing settings\n");

	if (!storage_ready())
	{
		DEBUG("Error: Storage not available\n");
		return false;
	}

	FIL * ee_file;
	ee_file = new FIL;

	if (f_open(ee_file, "CFG.EE", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		DEBUG("Unable to create file\n");
		delete ee_file;

		return false;
	}
	uint16_t wd = 0;

	eeprom_busy_wait();
	eeprom_update_dword(&config_ee.build_number, BUILD_NUMBER);

	uint16_t i = 0;
	do
	{
		uint8_t buf[256];
		uint16_t rwd;

		if (i + sizeof(buf) < sizeof(cfg_t))
			wd = sizeof(buf);
		else
			wd = sizeof(cfg_t) - i;


		eeprom_busy_wait();
		eeprom_read_block(buf, (uint8_t *)(APP_INFO_EE_offset + i), wd);


		assert(f_write(ee_file, buf, wd, &rwd) == FR_OK);

		i += wd;
	} while (i < sizeof(cfg_t));

	f_close(ee_file);
	DEBUG("File closed\n");

	delete ee_file;
	return true;
}

uint8_t flip_table[] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};

uint8_t fast_flip(uint8_t in)
{
	uint8_t out = flip_table[0x0F & in] << 4;
	out |= flip_table[(0xF0 & in) >> 4];
	return out;
}



void io_init()
{
	if (hw_revision == HW_REW_1504)
	{
		GpioSetDirection(IO1, OUTPUT);
	}

	if (hw_revision == HW_REW_1506)
	{
		GpioSetDirection(IO0, OUTPUT);
		GpioSetDirection(IO1, OUTPUT);
		GpioSetDirection(IO2, OUTPUT);
		GpioSetDirection(IO3, OUTPUT);
		GpioSetDirection(IO4, OUTPUT);
	}
}

void io_write(uint8_t io, uint8_t level)
{
	if (hw_revision == HW_REW_1504 && io == 1)
	{
		GpioWrite(IO1, level);
	}

	if (hw_revision == HW_REW_1506)
	{
		switch (io)
		{
			case(0):
				GpioWrite(IO0, level);
			break;
			case(1):
				GpioWrite(IO1, level);
			break;
			case(2):
				GpioWrite(IO2, level);
			break;
			case(3):
				GpioWrite(IO3, level);
			break;
			case(4):
				GpioWrite(IO4, level);
			break;
		}
	}
}

void mems_power_init()
{
	eeprom_busy_wait();
	hw_revision = eeprom_read_byte(&config_ro.hw_revision);

	GpioSetDirection(MEMS_EN, OUTPUT);

	if (hw_revision == HW_REW_1504)
	{
		GpioSetDirection(REV_1504_MEMS_EN_2, OUTPUT);
		GpioSetDirection(REV_1504_I2C_EN, OUTPUT);
	}
}

void mems_power_on()
{
	GpioWrite(MEMS_EN, HIGH);

	if (hw_revision == HW_REW_1504)
	{
		GpioWrite(REV_1504_MEMS_EN_2, HIGH);
		GpioWrite(REV_1504_I2C_EN, HIGH);
	}
}

void mems_power_off()
{
	GpioWrite(MEMS_EN, LOW);

	if (hw_revision == HW_REW_1504)
	{
		GpioWrite(REV_1504_MEMS_EN_2, LOW);
		GpioWrite(REV_1504_I2C_EN, LOW);
	}
}

float mul_to_sec(float mul)
{
	if (mul == 0)
		return 1;
	else
		return 1.0 / (mul * 100.0);
}

float sec_to_mul(float sec)
{
	if (sec == 0.0)
		return 1;
	else
		return 1.0 / (sec * 100.0);
}

