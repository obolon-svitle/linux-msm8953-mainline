
// SPDX-License-Identifier: GPL-2.0

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define OV12A10_EXPOSURE_MARGIN         8

#define OV12A10_REG_VALUE_08BIT		1
#define OV12A10_REG_VALUE_16BIT		2
#define OV12A10_REG_VALUE_24BIT		3

#define OV12A10_REG_MODE_SELECT		0x0100
#define OV12A10_MODE_STANDBY		0x00
#define OV12A10_MODE_STREAMING		0x01

#define OV12A10_REG_SOFTWARE_RST	0x0103
#define OV12A10_SOFTWARE_RST		0x01

#define OV12A10_REG_CHIP_ID		0x300a
#define OV12A10_CHIP_ID			0x561241

/* V_TIMING internal */
#define OV12A10_REG_VTS			0x380e
#define OV12A10_VTS_30FPS		0x0d34
#define OV12A10_VTS_MAX			0x7fff

/* Exposure control */
#define OV12A10_REG_EXPOSURE		0x3500
#define OV12A10_EXPOSURE_MIN		4
#define OV12A10_EXPOSURE_STEP		1
#define OV12A10_EXPOSURE_DEFAULT	0x4

/* Analog gain control */
#define OV12A10_REG_ANALOG_GAIN		0x3508
#define OV12A10_ANA_GAIN_MIN		0x0080
#define OV12A10_ANA_GAIN_MAX		0x0f80 // 15.5
#define OV12A10_ANA_GAIN_STEP		1
#define OV12A10_ANA_GAIN_DEFAULT	0x80

/* Digital gain control */
#define OV12A10_REG_DGTL_GAIN		0x350a

#define OV12A10_DGTL_GAIN_MIN		0x0400 /* Min = 1 X */
#define OV12A10_DGTL_GAIN_MAX		0x0fff /* Max = 4 X */
#define OV12A10_DGTL_GAIN_DEFAULT	0x0400 /* Default gain = 1 X */
#define OV12A10_DGTL_GAIN_STEP		1	/* Each step = 1/1024 */

/* Test Pattern Control */
#define OV12A10_REG_TEST_PATTERN	0x4503
#define OV12A10_TEST_PATTERN_ENABLE	BIT(7)
#define OV12A10_TEST_PATTERN_MASK	0x03
#define OV12A10_TEST_PATTERN_BAR_SHIFT	2

/* Flip Control */
#define OV12A10_REG_FORMAT1		0x3820

struct ov12a10_reg {
	u16 address;
	u8 val;
};

struct ov12a10_reg_list {
	u32 num_of_regs;
	const struct ov12a10_reg *regs;
};

struct ov12a10_link_freq_config {
	u64 link_freq;

	/* registers for this link frequency */
	struct ov12a10_reg_list reg_list;
};

struct ov12a10_mode {
	u32 width;
	u32 height;

	u32 vts_def;
	u32 vts_min;

	u32 link_freq_index;

	/* Pixel per line */
	u32 ppl;

	/* Default register values */
	struct ov12a10_reg_list reg_list;
};

/* 4096x3072 needs 1120Mbps/lane, 4 lanes */
static const struct ov12a10_reg mipi_data_rate_1120mbps[] = {
{0x0103, 0x01},
{0x0302, 0x49},
{0x0303, 0x00},
{0x0304, 0x00},
{0x0305, 0x01},
{0x0312, 0x11},
{0x030b, 0x06},
{0x030c, 0x02},
{0x030d, 0x88},
{0x030e, 0x02},
{0x0313, 0x05},
{0x3002, 0x21},
{0x3012, 0x40},
{0x3013, 0x72},
{0x3016, 0x72},
{0x301b, 0xd0},
{0x301d, 0xf0},
{0x301f, 0xd0},
{0x3021, 0x23},
{0x3022, 0x01},
{0x3106, 0x15},
{0x3107, 0x23},
{0x3500, 0x00},
{0x3501, 0xd0},
{0x3502, 0x00},
{0x3505, 0x83},
{0x3508, 0x02},
{0x3509, 0x00},
{0x3600, 0x43},
{0x3611, 0x8a},
{0x3613, 0x97},
{0x3620, 0x80},
{0x3624, 0x2c},
{0x3625, 0xa0},
{0x3626, 0x00},
{0x3631, 0x60},
{0x3632, 0x00},
{0x3641, 0x80},
{0x3642, 0x12},
{0x3644, 0x78},
{0x3645, 0xa7},
{0x364e, 0x44},
{0x364f, 0x44},
{0x3650, 0x11},
{0x3654, 0x00},
{0x3657, 0x31},
{0x3659, 0x0c},
{0x365f, 0x07},
{0x3661, 0x17},
{0x3662, 0x17},
{0x3663, 0x17},
{0x3664, 0x17},
{0x3666, 0x08},
{0x366b, 0x20},
{0x366c, 0xa4},
{0x366d, 0x20},
{0x366e, 0xa4},
{0x3680, 0x00},
{0x3714, 0x24},
{0x371a, 0x3e},
{0x3737, 0x04},
{0x3739, 0x12},
{0x3765, 0x20},
{0x3767, 0x00},
{0x37a1, 0x3e},
{0x37a8, 0x4d},
{0x37ab, 0x2c},
{0x37c2, 0x04},
{0x37d8, 0x03},
{0x37d9, 0x0c},
{0x37e0, 0x00},
{0x37e1, 0x0a},
{0x37e2, 0x14},
{0x37e3, 0x04},
{0x37e4, 0x2a},
{0x37e5, 0x03},
{0x37e6, 0x04},
{0x3800, 0x00},
{0x3801, 0x00},
{0x3802, 0x00},
{0x3803, 0x00},
{0x3804, 0x10},
{0x3805, 0x0f},
{0x3806, 0x0c},
{0x3807, 0x0f},
{0x3808, 0x10},
{0x3809, 0x00},
{0x380a, 0x0c},
{0x380b, 0x00},
{0x380c, 0x04},
{0x380d, 0x28},
{0x380e, 0x0d},
{0x380f, 0x12},
{0x3811, 0x0a},
{0x3813, 0x08},
{0x3814, 0x01},
{0x3815, 0x01},
{0x3816, 0x01},
{0x3817, 0x01},
{0x3820, 0xa8},
{0x3821, 0x00},
{0x3822, 0x91},
{0x3823, 0x18},
{0x3826, 0x00},
{0x3827, 0x00},
{0x3829, 0x03},
{0x3832, 0x08},
{0x3833, 0x30},
{0x3c80, 0x00},
{0x3c87, 0x01},
{0x3c8c, 0x1a},
{0x3c8d, 0x68},
{0x3c97, 0x02},
{0x3cc0, 0x40},
{0x3cc1, 0x54},
{0x3cc2, 0x34},
{0x3cc3, 0x04},
{0x3cc4, 0x00},
{0x3cc5, 0x00},
{0x3cc6, 0x00},
{0x3cc7, 0x00},
{0x3cc8, 0x00},
{0x3cc9, 0x00},
{0x3d8c, 0x73},
{0x3d8d, 0xc0},
{0x4001, 0x2b},
{0x4008, 0x02},
{0x4009, 0x0f},
{0x4011, 0xff},
{0x4013, 0x08},
{0x4014, 0x08},
{0x4015, 0x08},
{0x4017, 0x08},
{0x401a, 0x58},
{0x4050, 0x04},
{0x4051, 0x0b},
{0x405e, 0x20},
{0x4501, 0x00},
{0x4503, 0x00},
{0x450a, 0x04},
{0x4601, 0x30},
{0x4800, 0x00},
{0x481f, 0x30},
{0x4837, 0x0d},
{0x483c, 0x0f},
{0x484b, 0x01},
{0x4d00, 0x05},
{0x4d01, 0x19},
{0x4d02, 0xfd},
{0x4d03, 0xd1},
{0x4d04, 0xff},
{0x4d05, 0xff},
{0x5000, 0x09},
{0x5001, 0x42},
{0x5002, 0x45},
{0x5005, 0x00},
{0x5081, 0x04},
{0x5180, 0x00},
{0x5181, 0x10},
{0x5182, 0x02},
{0x5183, 0x0f},
{0x5185, 0x6c},
{0x5200, 0x03},
{0x5203, 0x7c},
{0x5204, 0x1f},
{0x5205, 0x41},
{0x520b, 0x07},
{0x520c, 0x0f},
{0x0300, 0x02},
{0x0302, 0x53},
{0x380c, 0x04},
{0x380d, 0x90},
{0x380e, 0x0c},
{0x380f, 0xe6},
{0x4837, 0x10},
};


static const struct ov12a10_reg mode_4096x3072_regs[] = {
	{0x0300, 0x06},
	{0x0301, 0x01},
	{0x0302, 0xf2},
	{0x0307, 0x01},
	{0x0303, 0x00},
	{0x0313, 0x05},
	{0x3642, 0x12},
	{0x3666, 0x08},
	{0x366b, 0x20},
	{0x366c, 0xa4},
	{0x366d, 0x20},
	{0x366e, 0xa4},
	{0x3714, 0x24},
	{0x371a, 0x3e},
	{0x3737, 0x04},
	{0x3739, 0x12},
	{0x37c2, 0x04},
	{0x37d9, 0x0c},
	{0x37e3, 0x04},
	{0x37e4, 0x2a},
	{0x37e6, 0x04},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x10},
	{0x3805, 0x0f},
	{0x3806, 0x0c},
	{0x3807, 0x0f},
	{0x3808, 0x10},
	{0x3809, 0x00},
	{0x380a, 0x0c},
	{0x380b, 0x00},
	{0x380c, 0x04},
	{0x380d, 0x90},
	{0x380e, 0x0e},
	{0x380f, 0x72},
	{0x3810, 0x00},
	{0x3811, 0x0a},
	{0x3813, 0x08},
	{0x3814, 0x01},
	{0x3816, 0x01},
	{0x3820, 0xa8},
	{0x4009, 0x0f},
	{0x4050, 0x04},
	{0x4051, 0x0b},
	{0x4501, 0x00},
	{0x4837, 0x10},
	{0x5001, 0x42},
	{0x5002, 0x45},
	{0x3501, 0xc0},
	{0x3502, 0x00},
	{0x3820, 0xa8},
	{0x3811, 0x0a},
	{0x3813, 0x08},

};

static const char * const ov12a10_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

static const u32 ov12a10_mbus_format = MEDIA_BUS_FMT_SBGGR10_1X10;

#define OV12A10_LINK_FREQ_560MHZ	560000000ULL
#define OV12A10_LINK_FREQ_INDEX_0	0

#define OV12A10_EXT_CLK			24000000
#define OV12A10_4_DATA_LANES		4

/*
 * pixel_rate = data_rate * nr_of_lanes / bits_per_pixel
 * data_rate => link_freq * 2; number of lanes => 4 or 2; bits per pixel => 10
 */
static u64 link_freq_to_pixel_rate(u64 f, u8 lanes)
{
	f *= 2 * lanes;
	do_div(f, 10);

	return f;
}

/* Menu items for LINK_FREQ V4L2 control */
static const s64 link_freq_menu_items[] = {
	OV12A10_LINK_FREQ_560MHZ
};

static const struct ov12a10_link_freq_config
			link_freq_configs[] = {
	{
		.link_freq = OV12A10_LINK_FREQ_560MHZ,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_1120mbps),
			.regs = mipi_data_rate_1120mbps,
		}
	}
};

static const struct ov12a10_mode supported_4_lanes_modes[] = {
	{
		.width = 4096,
		.height = 3072,
		.vts_def = OV12A10_VTS_30FPS,
		.vts_min = OV12A10_VTS_30FPS,
		.ppl = 4672,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_4096x3072_regs),
			.regs = mode_4096x3072_regs,
		},
		.link_freq_index = OV12A10_LINK_FREQ_INDEX_0,
	},
};

static const char * const ov12a10_supply_names[] = {
	"avdd",		/* Analog power */
	"dvdd",		/* Digital core power */
	"dovdd",	/* Digital I/O power */
	"afvcc",	/* Autofocus power */
};

#define OV12A10_NUM_SUPPLIES	ARRAY_SIZE(ov12a10_supply_names)

struct ov12a10 {
	struct device *dev;

	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_ctrl_handler ctrl_handler;

	struct clk *mclk;
	struct regulator_bulk_data supplies[OV12A10_NUM_SUPPLIES];
	struct gpio_desc *reset;

	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;

	const struct ov12a10_mode *supported_modes;

	const struct ov12a10_mode *cur_mode;

	u8 supported_modes_num;

	u8 data_lanes;
};

#define to_ov12a10(_sd)	container_of(_sd, struct ov12a10, sd)

/* Read registers up to 4 at a time */
static int ov12a10_read_reg(struct ov12a10 *ov12a10,
			    u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov12a10->sd);
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	int ret;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);

	if (len > 4)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

/* Write registers up to 4 at a time */
static int ov12a10_write_reg(struct ov12a10 *ov12a10,
			     u16 reg, u32 len, u32 __val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov12a10->sd);
	int buf_i, val_i;
	u8 buf[6], *val_p;
	__be32 val;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val = cpu_to_be32(__val);
	val_p = (u8 *)&val;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

/* Write a list of registers */
static int ov12a10_write_regs(struct ov12a10 *ov12a10,
			      const struct ov12a10_reg *regs, u32 len)
{
	int ret;
	u32 i;

	for (i = 0; i < len; i++) {
		ret = ov12a10_write_reg(ov12a10, regs[i].address, 1,
					regs[i].val);
		if (ret) {
			dev_err_ratelimited(ov12a10->dev,
					    "Failed to write reg 0x%4.4x. error = %d\n",
					    regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

static int ov12a10_write_reg_list(struct ov12a10 *ov12a10,
				  const struct ov12a10_reg_list *r_list)
{
	return ov12a10_write_regs(ov12a10, r_list->regs, r_list->num_of_regs);
}

static int ov12a10_update_digital_gain(struct ov12a10 *ov12a10, u32 d_gain)
{
	return ov12a10_write_reg(ov12a10, OV12A10_REG_DGTL_GAIN,
				OV12A10_REG_VALUE_16BIT, d_gain);
}

static int ov12a10_enable_test_pattern(struct ov12a10 *ov12a10, u32 pattern)
{
	int ret;
	u32 val;

	ret = ov12a10_read_reg(ov12a10, OV12A10_REG_TEST_PATTERN,
			       OV12A10_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	if (pattern) {
		val &= ~OV12A10_TEST_PATTERN_MASK;
		val |= (pattern - 1)  |
		     OV12A10_TEST_PATTERN_ENABLE;
	} else {
		val &= ~OV12A10_TEST_PATTERN_ENABLE;
	}

	return ov12a10_write_reg(ov12a10, OV12A10_REG_TEST_PATTERN,
				 OV12A10_REG_VALUE_08BIT, val);
}

static int ov12a10_set_ctrl_vflip(struct ov12a10 *ov12a10, u32 ctrl_val)
{
	int ret;
	u32 val;

	ret = ov12a10_read_reg(ov12a10, OV12A10_REG_FORMAT1,
			       OV12A10_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	if (ctrl_val)
		val |= BIT(4) | BIT(5);
	else
		val &= ~(BIT(4) | BIT(5));

	return ov12a10_write_reg(ov12a10, OV12A10_REG_FORMAT1,
				 OV12A10_REG_VALUE_08BIT,
				 val);
}

static int ov12a10_set_ctrl_hflip(struct ov12a10 *ov12a10, u32 ctrl_val)
{
	int ret;
	u32 val;

	ret = ov12a10_read_reg(ov12a10, OV12A10_REG_FORMAT1,
			       OV12A10_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	if (ctrl_val)
		val &= ~BIT(3);
	else
		val |= BIT(3);

	return ov12a10_write_reg(ov12a10, OV12A10_REG_FORMAT1,
				OV12A10_REG_VALUE_08BIT,
			      val);
}

static int ov12a10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov12a10 *ov12a10 = container_of(ctrl->handler,
					     struct ov12a10, ctrl_handler);
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov12a10->cur_mode->height + ctrl->val - OV12A10_EXPOSURE_MARGIN;
		__v4l2_ctrl_modify_range(ov12a10->exposure,
					 ov12a10->exposure->minimum,
					 max, ov12a10->exposure->step, max);
		break;
	}

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (!pm_runtime_get_if_in_use(ov12a10->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov12a10_write_reg(ov12a10, OV12A10_REG_ANALOG_GAIN,
					OV12A10_REG_VALUE_16BIT,
					ctrl->val);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = ov12a10_update_digital_gain(ov12a10, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		ret = ov12a10_write_reg(ov12a10, OV12A10_REG_EXPOSURE,
					OV12A10_REG_VALUE_24BIT,
					ctrl->val << 4);
		break;
	case V4L2_CID_VBLANK:
		ret = ov12a10_write_reg(ov12a10, OV12A10_REG_VTS,
					OV12A10_REG_VALUE_16BIT,
					ov12a10->cur_mode->height
					+ ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov12a10_enable_test_pattern(ov12a10, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ov12a10_set_ctrl_hflip(ov12a10, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ov12a10_set_ctrl_vflip(ov12a10, ctrl->val);
		break;
	default:
		dev_info(ov12a10->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n",
			 ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(ov12a10->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov12a10_ctrl_ops = {
	.s_ctrl = ov12a10_set_ctrl,
};

static int ov12a10_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = ov12a10_mbus_format;

	return 0;
}

static int ov12a10_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct ov12a10 *ov12a10 = to_ov12a10(sd);
	const struct ov12a10_mode *supported_modes = ov12a10->supported_modes;

	if (fse->index >= ov12a10->supported_modes_num)
		return -EINVAL;

	if (fse->code != ov12a10_mbus_format)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static void ov12a10_update_pad_format(const struct ov12a10_mode *mode,
				      struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = ov12a10_mbus_format;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int
ov12a10_set_pad_format(struct v4l2_subdev *sd,
		       struct v4l2_subdev_state *sd_state,
		       struct v4l2_subdev_format *fmt)
{
	struct ov12a10 *ov12a10 = to_ov12a10(sd);
	const struct ov12a10_mode *mode;
	const struct ov12a10_mode *supported_modes = ov12a10->supported_modes;
	s32 vblank_def;
	s32 vblank_min;
	s64 h_blank;
	s64 pixel_rate;
	s64 link_freq;

	mode = v4l2_find_nearest_size(supported_modes,
				      ov12a10->supported_modes_num,
				      width, height,
				      fmt->format.width, fmt->format.height);

	ov12a10_update_pad_format(mode, &fmt->format);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY || ov12a10->cur_mode == mode)
		goto set_format;

	ov12a10->cur_mode = mode;
	__v4l2_ctrl_s_ctrl(ov12a10->link_freq, mode->link_freq_index);
	link_freq = link_freq_menu_items[mode->link_freq_index];
	pixel_rate = link_freq_to_pixel_rate(link_freq,
					     ov12a10->data_lanes);
	__v4l2_ctrl_s_ctrl_int64(ov12a10->pixel_rate, pixel_rate);

	/* Update limits and set FPS to default */
	vblank_def = mode->vts_def - mode->height;
	vblank_min = mode->vts_min - mode->height;
	__v4l2_ctrl_modify_range(ov12a10->vblank, vblank_min,
				 OV12A10_VTS_MAX - mode->height,
				 1, vblank_def);
	__v4l2_ctrl_s_ctrl(ov12a10->vblank, vblank_def);
	h_blank = mode->ppl - mode->width;
	__v4l2_ctrl_modify_range(ov12a10->hblank, h_blank,
				 h_blank, 1, h_blank);

 set_format:
	*v4l2_subdev_state_get_format(sd_state, fmt->pad) = fmt->format;

	return 0;
}

static int ov12a10_identify_module(struct ov12a10 *ov12a10)
{
	int ret;
	u32 val;

	ret = ov12a10_read_reg(ov12a10, OV12A10_REG_CHIP_ID,
			       OV12A10_REG_VALUE_24BIT, &val);
	if (ret)
		return ret;

	if (val != OV12A10_CHIP_ID) {
		dev_err(ov12a10->dev, "chip id mismatch: %x!=%x\n",
			OV12A10_CHIP_ID, val);
		return -EIO;
	}

	return 0;
}

static int ov12a10_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov12a10 *ov12a10 = to_ov12a10(sd);

	gpiod_set_value_cansleep(ov12a10->reset, 1);
	regulator_bulk_disable(OV12A10_NUM_SUPPLIES, ov12a10->supplies);
	clk_disable_unprepare(ov12a10->mclk);

	return 0;
}

static int ov12a10_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov12a10 *ov12a10 = to_ov12a10(sd);
	int ret;

	ret = regulator_bulk_enable(OV12A10_NUM_SUPPLIES, ov12a10->supplies);
	if (ret) {
		return ret;
	}

	gpiod_set_value_cansleep(ov12a10->reset, 0);

	ret = clk_prepare_enable(ov12a10->mclk);
	if (ret < 0) {
		dev_err(dev, "failed to enable mclk: %d", ret);
		goto regulators_off;
	}

	/* 5ms to wait ready after XSHUTDN assert */
	usleep_range(5000, 5500);

	return 0;

regulators_off:
	regulator_bulk_disable(OV12A10_NUM_SUPPLIES, ov12a10->supplies);
	return ret;
}

static int ov12a10_start_streaming(struct ov12a10 *ov12a10)
{
	const struct ov12a10_reg_list *reg_list;
	int ret, link_freq_index;

	/* Get out of from software reset */
	ret = ov12a10_write_reg(ov12a10, OV12A10_REG_SOFTWARE_RST,
				OV12A10_REG_VALUE_08BIT, OV12A10_SOFTWARE_RST);
	if (ret) {
		dev_err(ov12a10->dev, "%s failed to set powerup registers\n",
			__func__);
		return ret;
	}

	link_freq_index = ov12a10->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;
	ret = ov12a10_write_reg_list(ov12a10, reg_list);
	if (ret) {
		dev_err(ov12a10->dev, "%s failed to set plls\n", __func__);
		return ret;
	}

	/* Apply default values of current mode */
	reg_list = &ov12a10->cur_mode->reg_list;
	ret = ov12a10_write_reg_list(ov12a10, reg_list);
	if (ret) {
		dev_err(ov12a10->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(ov12a10->sd.ctrl_handler);
	if (ret)
		return ret;

	return ov12a10_write_reg(ov12a10, OV12A10_REG_MODE_SELECT,
				 OV12A10_REG_VALUE_08BIT,
				 OV12A10_MODE_STREAMING);
}

static int ov12a10_get_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	struct ov12a10 *ov12a10 = to_ov12a10(sd);

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = ov12a10->cur_mode->width;
		sel->r.height = ov12a10->cur_mode->height;
		return 0;
	default:
		return -EINVAL;
	}
}


static int ov12a10_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct ov12a10 *ov12a10 = to_ov12a10(sd);
	int ret;

	ret = ov12a10_write_reg(ov12a10, OV12A10_REG_MODE_SELECT,
				OV12A10_REG_VALUE_08BIT, OV12A10_MODE_STANDBY);
	pm_runtime_put(ov12a10->dev);

	return ret;
}

static int ov12a10_enable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct ov12a10 *ov12a10 = to_ov12a10(sd);
	int ret;

	ret = pm_runtime_resume_and_get(ov12a10->dev);
	if (ret < 0)
		return ret;

	ret = ov12a10_start_streaming(ov12a10);
	if (ret)
		goto err_rpm_put;

	return ret;

err_rpm_put:
	pm_runtime_put(ov12a10->dev);
	return ret;
}

static int ov12a10_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	struct ov12a10 *ov12a10 = to_ov12a10(sd);
	const struct ov12a10_mode *default_mode = ov12a10->supported_modes;
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.pad = 0,
		.format = {
			.width = default_mode->width,
			.height = default_mode->height,
		},
	};

	ov12a10_set_pad_format(sd, state, &fmt);

	return 0;
}

static const struct v4l2_subdev_pad_ops ov12a10_pad_ops = {
	.enum_mbus_code = ov12a10_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ov12a10_set_pad_format,
	.get_selection = ov12a10_get_selection,
	.enum_frame_size = ov12a10_enum_frame_size,
	.enable_streams = ov12a10_enable_streams,
	.disable_streams = ov12a10_disable_streams,
};

static const struct v4l2_subdev_video_ops ov12a10_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_ops ov12a10_subdev_ops = {
	.video = &ov12a10_video_ops,
	.pad = &ov12a10_pad_ops,
};

static const struct media_entity_operations ov12a10_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops ov12a10_internal_ops = {
	.init_state = ov12a10_init_state,
};

static int ov12a10_init_controls(struct ov12a10 *ov12a10)
{
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max;
	s64 vblank_def;
	s64 vblank_min;
	s64 hblank;
	s64 pixel_rate_max;
	const struct ov12a10_mode *mode;
	u32 max;
	int ret;

	ctrl_hdlr = &ov12a10->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 10);
	if (ret)
		return ret;

	max = ARRAY_SIZE(link_freq_menu_items) - 1;
	ov12a10->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr,
						  &ov12a10_ctrl_ops,
						  V4L2_CID_LINK_FREQ,
						  max,
						  0,
						  link_freq_menu_items);
	if (ov12a10->link_freq)
		ov12a10->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate_max = link_freq_to_pixel_rate(link_freq_menu_items[0],
						 ov12a10->data_lanes);

	ov12a10->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov12a10_ctrl_ops,
					      V4L2_CID_PIXEL_RATE,
					      0, pixel_rate_max,
					      1, pixel_rate_max);

	mode = ov12a10->cur_mode;
	vblank_def = mode->vts_def - mode->height;
	vblank_min = mode->vts_min - mode->height;
	ov12a10->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov12a10_ctrl_ops,
					  V4L2_CID_VBLANK,
					  vblank_min,
					  OV12A10_VTS_MAX - mode->height, 1,
					  vblank_def);

	hblank = mode->ppl - mode->width;
	ov12a10->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov12a10_ctrl_ops,
					  V4L2_CID_HBLANK,
					  hblank, hblank, 1, hblank);
	if (ov12a10->hblank)
		ov12a10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	exposure_max = mode->vts_def - OV12A10_EXPOSURE_MARGIN;
	ov12a10->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ov12a10_ctrl_ops,
					    V4L2_CID_EXPOSURE,
					    OV12A10_EXPOSURE_MIN,
					    exposure_max, OV12A10_EXPOSURE_STEP,
					    exposure_max);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov12a10_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV12A10_ANA_GAIN_MIN, OV12A10_ANA_GAIN_MAX,
			  OV12A10_ANA_GAIN_STEP, OV12A10_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov12a10_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV12A10_DGTL_GAIN_MIN, OV12A10_DGTL_GAIN_MAX,
			  OV12A10_DGTL_GAIN_STEP, OV12A10_DGTL_GAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov12a10_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov12a10_test_pattern_menu) - 1,
				     0, 0, ov12a10_test_pattern_menu);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov12a10_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov12a10_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(ov12a10->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(ov12a10->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ov12a10_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	ov12a10->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static int ov12a10_get_pm_resources(struct ov12a10 *ov12a10)
{
	int ret;
	unsigned int i;

	ov12a10->reset = devm_gpiod_get_optional(ov12a10->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ov12a10->reset))
		return dev_err_probe(ov12a10->dev, PTR_ERR(ov12a10->reset),
				     "failed to get reset gpio\n");

	ov12a10->mclk = devm_v4l2_sensor_clk_get(ov12a10->dev, NULL);
	if (IS_ERR(ov12a10->mclk))
		return dev_err_probe(ov12a10->dev, PTR_ERR(ov12a10->mclk),
				     "failed to get imaging clock\n");


	ret = clk_set_rate(ov12a10->mclk, OV12A10_EXT_CLK);
	if (ret) {
		return dev_err_probe(ov12a10->dev, -EINVAL,
				     "external clock is not supported\n");
	}

	for (i = 0; i < OV12A10_NUM_SUPPLIES; i++)
		ov12a10->supplies[i].supply = ov12a10_supply_names[i];

	ret = devm_regulator_bulk_get(ov12a10->dev, OV12A10_NUM_SUPPLIES,
				      ov12a10->supplies);
	if (ret)
		return dev_err_probe(ov12a10->dev, ret,
				     "failed to get supply regulators\n");

	return 0;
}

static int ov12a10_check_hwcfg(struct ov12a10 *ov12a10)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct device *dev = ov12a10->dev;
	struct fwnode_handle *ep;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	unsigned int i, j;
	int ret;
	u8 dlane;

	if (!fwnode)
		return -ENODEV;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -EPROBE_DEFER;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	dlane = bus_cfg.bus.mipi_csi2.num_data_lanes;
	switch (dlane) {
	case OV12A10_4_DATA_LANES:
		ov12a10->supported_modes = supported_4_lanes_modes;
		ov12a10->supported_modes_num =
			ARRAY_SIZE(supported_4_lanes_modes);
		break;

	default:
		dev_err(dev, "number of CSI2 data lanes %d is not supported",
			dlane);
		ret = -EINVAL;
		goto out_err;
	}

	ov12a10->data_lanes = dlane;
	ov12a10->cur_mode = ov12a10->supported_modes;

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(dev, "no link frequencies defined");
		ret = -EINVAL;
		goto out_err;
	}

	for (i = 0; i < ARRAY_SIZE(link_freq_menu_items); i++) {
		for (j = 0; j < bus_cfg.nr_of_link_frequencies; j++) {
			if (link_freq_menu_items[i] ==
				bus_cfg.link_frequencies[j])
				break;
		}

		if (j == bus_cfg.nr_of_link_frequencies) {
			dev_err(dev, "no link frequency %lld supported",
				link_freq_menu_items[i]);
			ret = -EINVAL;
			goto out_err;
		}
	}

out_err:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int ov12a10_probe(struct i2c_client *client)
{
	struct ov12a10 *ov12a10;
	int ret;

	ov12a10 = devm_kzalloc(&client->dev, sizeof(*ov12a10), GFP_KERNEL);
	if (!ov12a10)
		return -ENOMEM;

	ov12a10->dev = &client->dev;

	ret = ov12a10_check_hwcfg(ov12a10);
	if (ret) {
		dev_err(ov12a10->dev, "failed to check hwcfg: %d", ret);
		return ret;
	}

	v4l2_i2c_subdev_init(&ov12a10->sd, client, &ov12a10_subdev_ops);

	ret = ov12a10_get_pm_resources(ov12a10);
	if (ret)
		return ret;

	ret = ov12a10_power_on(ov12a10->dev);
	if (ret) {
		dev_err(ov12a10->dev, "failed to power on\n");
		return ret;
	}

	ret = ov12a10_identify_module(ov12a10);
	if (ret) {
		dev_err(ov12a10->dev, "failed to find sensor: %d\n", ret);
		goto error_power_off;
	}

	ret = ov12a10_init_controls(ov12a10);
	if (ret)
		goto error_power_off;

	ov12a10->sd.state_lock = ov12a10->ctrl_handler.lock;
	ov12a10->sd.internal_ops = &ov12a10_internal_ops;
	ov12a10->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov12a10->sd.entity.ops = &ov12a10_subdev_entity_ops;
	ov12a10->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov12a10->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&ov12a10->sd.entity, 1, &ov12a10->pad);
	if (ret) {
		dev_err(ov12a10->dev, "%s failed:%d\n", __func__, ret);
		goto error_handler_free;
	}

	ret = v4l2_subdev_init_finalize(&ov12a10->sd);
	if (ret < 0) {
		dev_err_probe(ov12a10->dev, ret,
			      "failed to init media entity pads\n");
		goto media_entity_cleanup;
	}

	pm_runtime_set_active(ov12a10->dev);
	pm_runtime_enable(ov12a10->dev);

	ret = v4l2_async_register_subdev_sensor(&ov12a10->sd);
	if (ret < 0)
		goto error_media_entity_runtime_pm;

	pm_runtime_set_autosuspend_delay(ov12a10->dev, 1000);
	pm_runtime_use_autosuspend(ov12a10->dev);
	pm_runtime_idle(ov12a10->dev);

	return 0;

error_media_entity_runtime_pm:
	v4l2_subdev_cleanup(&ov12a10->sd);
	pm_runtime_disable(ov12a10->dev);
	pm_runtime_set_suspended(ov12a10->dev);

media_entity_cleanup:
	media_entity_cleanup(&ov12a10->sd.entity);

error_handler_free:
	v4l2_ctrl_handler_free(ov12a10->sd.ctrl_handler);

error_power_off:
	ov12a10_power_off(ov12a10->dev);

	return ret;
}

static void ov12a10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov12a10 *ov12a10 = to_ov12a10(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(ov12a10->dev);

	if (!pm_runtime_status_suspended(&client->dev)) {
		ov12a10_power_off(&client->dev);
		pm_runtime_set_suspended(ov12a10->dev);
	}
}

static DEFINE_RUNTIME_DEV_PM_OPS(ov12a10_pm_ops, ov12a10_power_off,
				 ov12a10_power_on, NULL);

static const struct of_device_id ov12a10_of_match[] = {
	{ .compatible = "ov12a10" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, ov12a10_of_match);

static struct i2c_driver ov12a10_i2c_driver = {
	.driver = {
		.name = "ov12a10",
		.pm = &ov12a10_pm_ops,
		.of_match_table = ov12a10_of_match,
	},
	.probe = ov12a10_probe,
	.remove = ov12a10_remove,
};

module_i2c_driver(ov12a10_i2c_driver);

MODULE_AUTHOR("Andrii Cherniavskyi <chernyav.a@gmail.com>");
MODULE_DESCRIPTION("OmniVision OV12A10 image sensor driver");
MODULE_LICENSE("GPL");
