#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/time.h>
#include "kd_flashlight.h"
#include <asm/io.h>
#include <asm/uaccess.h>
#include "kd_camera_typedef.h"
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/leds.h>



/******************************************************************************
 * Debug configuration
******************************************************************************/
/* availible parameter */
/* ANDROID_LOG_ASSERT */
/* ANDROID_LOG_ERROR */
/* ANDROID_LOG_WARNING */
/* ANDROID_LOG_INFO */
/* ANDROID_LOG_DEBUG */
/* ANDROID_LOG_VERBOSE */

#define TAG_NAME "[leds_strobe.c]"
#define PK_DBG_NONE(fmt, arg...)    do {} while (0)
#define PK_DBG_FUNC(fmt, arg...)    pr_debug(TAG_NAME "%s: " fmt, __func__ , ##arg)

/*#define DEBUG_LEDS_STROBE*/
#ifdef DEBUG_LEDS_STROBE
#define PK_DBG PK_DBG_FUNC
#else
#define PK_DBG(a, ...)
#endif

#define GPIO_OUT_ONE 1
#define GPIO_OUT_ZERO 0

/******************************************************************************
 * local variables
******************************************************************************/

static DEFINE_SPINLOCK(g_strobeSMPLock);	/* cotta-- SMP proection */


static u32 strobe_Res;
static u32 strobe_Timeus;
static BOOL g_strobe_On;

static int g_duty = -1;
static int g_timeOutTimeMs;

static DEFINE_MUTEX(g_strobeSem);


#define STROBE_DEVICE_ID 0xC6


static struct work_struct workTimeOut;

/* #define FLASH_GPIO_ENF GPIO12 */
/* #define FLASH_GPIO_ENT GPIO13 */

#define USE_FLASHLIGHT	//add by liuzhen
#define USE_FLASHLIGHT_NODE	//add by liuzhen

#ifdef USE_FLASHLIGHT
#else
static int g_bLtVersion;
#endif

/*****************************************************************************
Functions
*****************************************************************************/
static void work_timeOutFunc(struct work_struct *data);

static struct i2c_client *LM3642_i2c_client;

//add by liuzhen,20151214 start
#ifdef USE_FLASHLIGHT
#define GPIO_OUT_ONE 1
#define GPIO_OUT_ZERO 0
typedef enum _FLASHLIGHT_CTRL_
{
	GPIO_CAMERA_FLASH_MODE_PIN = 0,
	GPIO_CAMERA_FLASH_EN_PIN
}FLASHLIGHT_CTRL;

/* GPIO Pin control*/
struct platform_device *flashlight_plt_dev = NULL;
struct pinctrl *flctrl = NULL;
struct pinctrl_state *fl0_mode_h = NULL;
struct pinctrl_state *fl0_mode_l = NULL;
struct pinctrl_state *fl0_en_h = NULL;
struct pinctrl_state *fl0_en_l = NULL;

int mtkflashlight_gpio_init(struct platform_device *pdev)
{
	int ret = 0;

	flctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(flctrl)) {
		dev_err(&pdev->dev, "Cannot find flashlight pinctrl!");
		ret = PTR_ERR(flctrl);
	}
	/*Cam0 Power/Rst Ping initialization */
	fl0_mode_h = pinctrl_lookup_state(flctrl, "flashlight0_mode1");
	if (IS_ERR(fl0_mode_h)) {
		ret = PTR_ERR(fl0_mode_h);
		pr_debug("%s : pinctrl err, fl0_mode_h\n", __func__);
	}

	fl0_mode_l = pinctrl_lookup_state(flctrl, "flashlight0_mode0");
	if (IS_ERR(fl0_mode_l)) {
		ret = PTR_ERR(fl0_mode_l);
		pr_debug("%s : pinctrl err, fl0_mode_l\n", __func__);
	}


	fl0_en_h = pinctrl_lookup_state(flctrl, "flashlight0_en1");
	if (IS_ERR(fl0_en_h)) {
		ret = PTR_ERR(fl0_en_h);
		pr_debug("%s : pinctrl err, fl0_en_h\n", __func__);
	}

	fl0_en_l = pinctrl_lookup_state(flctrl, "flashlight0_en0");
	if (IS_ERR(fl0_en_l)) {
		ret = PTR_ERR(fl0_en_l);
		pr_debug("%s : pinctrl err, fl0_en_l\n", __func__);
	}

	return ret;
}
		
static int mtkflashlight_gpio_set(int pin, int state)
{
	int ret = 0;

	switch (pin) {
	case GPIO_CAMERA_FLASH_MODE_PIN:
		if (state == GPIO_OUT_ZERO)
			pinctrl_select_state(flctrl, fl0_mode_l);
		else
			pinctrl_select_state(flctrl, fl0_mode_h);
	
		break;
	case GPIO_CAMERA_FLASH_EN_PIN:
		if (state == GPIO_OUT_ZERO)
			pinctrl_select_state(flctrl, fl0_en_l);
		else
			pinctrl_select_state(flctrl, fl0_en_h);

		break;
	default:
		PK_DBG("pin(%d) is invalid !!\n", pin);
		break;
	};

	printk("pin(%d) state(%d)\n", pin, state);

	return ret;
}
#endif
//add end

struct LM3642_platform_data {
	u8 torch_pin_enable;	/* 1:  TX1/TORCH pin isa hardware TORCH enable */
	u8 pam_sync_pin_enable;	/* 1:  TX2 Mode The ENVM/TX2 is a PAM Sync. on input */
	u8 thermal_comp_mode_enable;	/* 1: LEDI/NTC pin in Thermal Comparator Mode */
	u8 strobe_pin_disable;	/* 1 : STROBE Input disabled */
	u8 vout_mode_enable;	/* 1 : Voltage Out Mode enable */
};

struct LM3642_chip_data {
	struct i2c_client *client;

	/* struct led_classdev cdev_flash; */
	/* struct led_classdev cdev_torch; */
	/* struct led_classdev cdev_indicator; */

	struct LM3642_platform_data *pdata;
	struct mutex lock;

	u8 last_flag;
	u8 no_pdata;
};
#ifdef USE_FLASHLIGHT
#else
static int LM3642_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret = 0;
	struct LM3642_chip_data *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	ret = i2c_smbus_write_byte_data(client, reg, val);
	mutex_unlock(&chip->lock);

	if (ret < 0)
		PK_DBG("failed writing at 0x%02x\n", reg);
	return ret;
}
#endif
static int LM3642_read_reg(struct i2c_client *client, u8 reg)
{
	int val = 0;
	struct LM3642_chip_data *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	val = i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&chip->lock);

	return val;
}


static int LM3642_chip_init(struct LM3642_chip_data *chip)
{


	return 0;
}

static int LM3642_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct LM3642_chip_data *chip;
	struct LM3642_platform_data *pdata = client->dev.platform_data;

	int err = -1;

	PK_DBG("LM3642_probe start--->.\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		PK_DBG("LM3642 i2c functionality check fail.\n");
		return err;
	}

	chip = kzalloc(sizeof(struct LM3642_chip_data), GFP_KERNEL);
	chip->client = client;

	mutex_init(&chip->lock);
	i2c_set_clientdata(client, chip);

	if (pdata == NULL) {	/* values are set to Zero. */
		PK_DBG("LM3642 Platform data does not exist\n");
		pdata = kzalloc(sizeof(struct LM3642_platform_data), GFP_KERNEL);
		chip->pdata = pdata;
		chip->no_pdata = 1;
	}

	chip->pdata = pdata;
	if (LM3642_chip_init(chip) < 0)
		goto err_chip_init;

	LM3642_i2c_client = client;
	PK_DBG("LM3642 Initializing is done\n");

	return 0;

err_chip_init:
	i2c_set_clientdata(client, NULL);
	kfree(chip);
	PK_DBG("LM3642 probe is failed\n");
	return -ENODEV;
}

static int LM3642_remove(struct i2c_client *client)
{
	struct LM3642_chip_data *chip = i2c_get_clientdata(client);

	if (chip->no_pdata)
		kfree(chip->pdata);
	kfree(chip);
	return 0;
}


#define LM3642_NAME "leds-LM3642"
static const struct i2c_device_id LM3642_id[] = {
	{LM3642_NAME, 0},
	{}
};

#ifdef CONFIG_OF
static const struct of_device_id LM3642_of_match[] = {
	{.compatible = "mediatek,strobe_main"},
	{},
};
#endif

static struct i2c_driver LM3642_i2c_driver = {
	.driver = {
		   .name = LM3642_NAME,
#ifdef CONFIG_OF
		   .of_match_table = LM3642_of_match,
#endif
		   },
	.probe = LM3642_probe,
	.remove = LM3642_remove,
	.id_table = LM3642_id,
};
static int __init LM3642_init(void)
{
	PK_DBG("LM3642_init\n");
	return i2c_add_driver(&LM3642_i2c_driver);
}

static void __exit LM3642_exit(void)
{
	i2c_del_driver(&LM3642_i2c_driver);
}


module_init(LM3642_init);
module_exit(LM3642_exit);

MODULE_DESCRIPTION("Flash driver for LM3642");
MODULE_AUTHOR("pw <pengwei@mediatek.com>");
MODULE_LICENSE("GPL v2");

int readReg(int reg)
{

	int val;

	val = LM3642_read_reg(LM3642_i2c_client, reg);
	return (int)val;
}
#ifdef  USE_FLASHLIGHT_NODE  //add for Main flashlight node by liuzhen
int FL_set_mainflashlight(unsigned int onOff)
{
	printk("<%s:%d>%d\n", __func__, __LINE__, onOff);
	if(onOff){
		mtkflashlight_gpio_set(GPIO_CAMERA_FLASH_MODE_PIN,GPIO_OUT_ONE);
		mtkflashlight_gpio_set(GPIO_CAMERA_FLASH_EN_PIN,GPIO_OUT_ZERO);
	}else{
		mtkflashlight_gpio_set(GPIO_CAMERA_FLASH_MODE_PIN,GPIO_OUT_ZERO);
		mtkflashlight_gpio_set(GPIO_CAMERA_FLASH_EN_PIN,GPIO_OUT_ZERO);
	}
	return 0;
}
EXPORT_SYMBOL(FL_set_mainflashlight);
#endif 

#ifdef USE_FLASHLIGHT
//modify by liuzhen
int FL_Enable(void)
{
	printk(" FL_Enable,g_duty = %d, line=%d\n",g_duty,__LINE__);
	if(g_duty==0)	//torch,pf
	{
		mtkflashlight_gpio_set(GPIO_CAMERA_FLASH_MODE_PIN,GPIO_OUT_ONE);
		mtkflashlight_gpio_set(GPIO_CAMERA_FLASH_EN_PIN,GPIO_OUT_ZERO);
		printk(" FL_Enable torch mode, line=%d\n",__LINE__);
	}
	else		//mf
	{
		mtkflashlight_gpio_set(GPIO_CAMERA_FLASH_MODE_PIN,GPIO_OUT_ZERO);
		mtkflashlight_gpio_set(GPIO_CAMERA_FLASH_EN_PIN,GPIO_OUT_ONE);
		printk(" FL_Enable flashlight mode,line=%d\n",__LINE__);
	}

    return 0;
}

int FL_Disable(void)
{

	mtkflashlight_gpio_set(GPIO_CAMERA_FLASH_MODE_PIN,GPIO_OUT_ZERO);
	mtkflashlight_gpio_set(GPIO_CAMERA_FLASH_EN_PIN,GPIO_OUT_ZERO);
	printk(" FL_Disable line=%d\n",__LINE__);
    return 0;
}
#else
int FL_Enable(void)
{
	char buf[2];
/* char bufR[2]; */
	if (g_duty < 0)
		g_duty = 0;
	else if (g_duty > 16)
		g_duty = 16;
	if (g_duty <= 2) {
		int val;

		if (g_bLtVersion == 1) {
			if (g_duty == 0)
				val = 3;
			else if (g_duty == 1)
				val = 5;
			else	/* if(g_duty==2) */
				val = 7;
		} else {
			if (g_duty == 0)
				val = 1;
			else if (g_duty == 1)
				val = 2;
			else	/* if(g_duty==2) */
				val = 3;
		}
		buf[0] = 9;
		buf[1] = val << 4;
		/* iWriteRegI2C(buf , 2, STROBE_DEVICE_ID); */
		LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);

		buf[0] = 10;
		buf[1] = 0x02;
		/* iWriteRegI2C(buf , 2, STROBE_DEVICE_ID); */
		LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);
	} else {
		int val;

		val = (g_duty - 1);
		buf[0] = 9;
		buf[1] = val;
		/* iWriteRegI2C(buf , 2, STROBE_DEVICE_ID); */
		LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);

		buf[0] = 10;
		buf[1] = 0x03;
		/* iWriteRegI2C(buf , 2, STROBE_DEVICE_ID); */
		LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);
	}
	PK_DBG(" FL_Enable line=%d\n", __LINE__);

	readReg(0);
	readReg(1);
	readReg(6);
	readReg(8);
	readReg(9);
	readReg(0xa);
	readReg(0xb);

	return 0;
}



int FL_Disable(void)
{
	char buf[2];

/* ///////////////////// */
	buf[0] = 10;
	buf[1] = 0x00;
	/* iWriteRegI2C(buf , 2, STROBE_DEVICE_ID); */
	LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);
	PK_DBG(" FL_Disable line=%d\n", __LINE__);
	return 0;
}
#endif

int FL_dim_duty(kal_uint32 duty)
{
	printk(" FL_dim_duty duty=%d,line=%d\n",duty, __LINE__);
	g_duty = duty;
	return 0;
}


//modify by liuzhen
#ifdef USE_FLASHLIGHT
int FL_Init(void)
{
    /*Init. to disable*/
    if(mtkflashlight_gpio_set(GPIO_CAMERA_FLASH_MODE_PIN,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
    if(mtkflashlight_gpio_set(GPIO_CAMERA_FLASH_EN_PIN,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}

    INIT_WORK(&workTimeOut, work_timeOutFunc);
    PK_DBG(" FL_Init line=%d\n",__LINE__);
    return 0;
}

int FL_Uninit(void)
{
    PK_DBG(" FL_Uninit line=%d\n",__LINE__);
    /*Uninit. to disable*/
    if(mtkflashlight_gpio_set(GPIO_CAMERA_FLASH_EN_PIN,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
    return 0;
}
#else
int FL_Init(void)
{
	int regVal0;
	char buf[2];

	buf[0] = 0xa;
	buf[1] = 0x0;
	/* iWriteRegI2C(buf , 2, STROBE_DEVICE_ID); */
	LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);

	buf[0] = 0x8;
	buf[1] = 0x47;
	/* iWriteRegI2C(buf , 2, STROBE_DEVICE_ID); */
	LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);

	buf[0] = 9;
	buf[1] = 0x35;
	/* iWriteRegI2C(buf , 2, STROBE_DEVICE_ID); */
	LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);




	/* static int LM3642_read_reg(struct i2c_client *client, u8 reg) */
	/* regVal0 = readReg(0); */
	regVal0 = LM3642_read_reg(LM3642_i2c_client, 0);

	if (regVal0 == 1)
		g_bLtVersion = 1;
	else
		g_bLtVersion = 0;


	PK_DBG(" FL_Init regVal0=%d isLtVer=%d\n", regVal0, g_bLtVersion);


/*
	if(mt_set_gpio_mode(FLASH_GPIO_ENT,GPIO_MODE_00)){PK_DBG("[constant_flashlight] set gpio mode failed!!\n");}
    if(mt_set_gpio_dir(FLASH_GPIO_ENT,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!!\n");}
    if(mtkflashlight_gpio_set(FLASH_GPIO_ENT,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!!\n");}

	if(mt_set_gpio_mode(FLASH_GPIO_ENF,GPIO_MODE_00)){PK_DBG("[constant_flashlight] set gpio mode failed!!\n");}
    if(mt_set_gpio_dir(FLASH_GPIO_ENF,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!!\n");}
    if(mtkflashlight_gpio_set(FLASH_GPIO_ENF,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!!\n");}
    */


/*	PK_DBG(" FL_Init line=%d\n", __LINE__); */
	return 0;
}

int FL_Uninit(void)
{
	FL_Disable();
	return 0;
}
#endif

/*****************************************************************************
User interface
*****************************************************************************/

static void work_timeOutFunc(struct work_struct *data)
{
	FL_Disable();
	PK_DBG("ledTimeOut_callback\n");
}



enum hrtimer_restart ledTimeOutCallback(struct hrtimer *timer)
{
	schedule_work(&workTimeOut);
	return HRTIMER_NORESTART;
}

static struct hrtimer g_timeOutTimer;
void timerInit(void)
{
	INIT_WORK(&workTimeOut, work_timeOutFunc);
	g_timeOutTimeMs = 1000;
	hrtimer_init(&g_timeOutTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	g_timeOutTimer.function = ledTimeOutCallback;
}



static int constant_flashlight_ioctl(unsigned int cmd, unsigned long arg)
{
	int i4RetValue = 0;
	int ior_shift;
	int iow_shift;
	int iowr_shift;

	ior_shift = cmd - (_IOR(FLASHLIGHT_MAGIC, 0, int));
	iow_shift = cmd - (_IOW(FLASHLIGHT_MAGIC, 0, int));
	iowr_shift = cmd - (_IOWR(FLASHLIGHT_MAGIC, 0, int));
/*	PK_DBG
	    ("LM3642 constant_flashlight_ioctl() line=%d ior_shift=%d, iow_shift=%d iowr_shift=%d arg=%d\n",
	     __LINE__, ior_shift, iow_shift, iowr_shift, (int)arg);
*/
	switch (cmd) {

	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		PK_DBG("FLASH_IOC_SET_TIME_OUT_TIME_MS: %d\n", (int)arg);
		g_timeOutTimeMs = arg;
		break;


	case FLASH_IOC_SET_DUTY:
		PK_DBG("FLASHLIGHT_DUTY: %d\n", (int)arg);
		FL_dim_duty(arg);
		break;


	case FLASH_IOC_SET_STEP:
		PK_DBG("FLASH_IOC_SET_STEP: %d\n", (int)arg);

		break;

	case FLASH_IOC_SET_ONOFF:
		PK_DBG("FLASHLIGHT_ONOFF: %d\n", (int)arg);
		if (arg == 1) {

			int s;
			int ms;

			if (g_timeOutTimeMs > 1000) {
				s = g_timeOutTimeMs / 1000;
				ms = g_timeOutTimeMs - s * 1000;
			} else {
				s = 0;
				ms = g_timeOutTimeMs;
			}

			if (g_timeOutTimeMs != 0) {
				ktime_t ktime;

				ktime = ktime_set(s, ms * 1000000);
				hrtimer_start(&g_timeOutTimer, ktime, HRTIMER_MODE_REL);
			}
			FL_Enable();
		} else {
			FL_Disable();
			hrtimer_cancel(&g_timeOutTimer);
		}
		break;
	default:
		PK_DBG(" No such command\n");
		i4RetValue = -EPERM;
		break;
	}
	return i4RetValue;
}




static int constant_flashlight_open(void *pArg)
{
	int i4RetValue = 0;

	PK_DBG("constant_flashlight_open line=%d\n", __LINE__);

	if (0 == strobe_Res) {
		FL_Init();
		timerInit();
	}
	PK_DBG("constant_flashlight_open line=%d\n", __LINE__);
	spin_lock_irq(&g_strobeSMPLock);


	if (strobe_Res) {
		PK_DBG(" busy!\n");
		i4RetValue = -EBUSY;
	} else {
		strobe_Res += 1;
	}


	spin_unlock_irq(&g_strobeSMPLock);
	PK_DBG("constant_flashlight_open line=%d\n", __LINE__);

	return i4RetValue;

}


static int constant_flashlight_release(void *pArg)
{
	PK_DBG(" constant_flashlight_release\n");

	if (strobe_Res) {
		spin_lock_irq(&g_strobeSMPLock);

		strobe_Res = 0;
		strobe_Timeus = 0;

		/* LED On Status */
		g_strobe_On = FALSE;

		spin_unlock_irq(&g_strobeSMPLock);

		FL_Uninit();
	}

	PK_DBG(" Done\n");

	return 0;

}


FLASHLIGHT_FUNCTION_STRUCT constantFlashlightFunc = {
	constant_flashlight_open,
	constant_flashlight_release,
	constant_flashlight_ioctl
};


MUINT32 constantFlashlightInit(PFLASHLIGHT_FUNCTION_STRUCT *pfFunc)
{
	if (pfFunc != NULL)
		*pfFunc = &constantFlashlightFunc;
	return 0;
}



/* LED flash control for high current capture mode*/
ssize_t strobe_VDIrq(void)
{

	return 0;
}
EXPORT_SYMBOL(strobe_VDIrq);
