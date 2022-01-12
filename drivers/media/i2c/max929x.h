#ifndef __MAX929X_H__
#define __MAX929X_H__

#define AR0239_TABLE_END 0xffff

struct max929x_reg {
	u16 slave_addr;
	u16 reg;
	u16 val;
};

/*
 * MAX9296 i2c addr 0x90(8bits) 0x48(7bits)
 * (MAX9296 link A) MAX9295 i2c addr 0xc0(8bits) 0x60(7bits)
 * (MAX9296 link B) MAX9295 i2c addr 0xc4(8bits) 0x62(7bits)
 */
static struct max929x_reg max929x_Double_Dser_Ser_init[] = {
	{0x48, 0x0010, 0x21}, //reset linkA data path
	{0x62, 0x0000, 0xc0}, //change linka 9295 i2c address 0xc0

	{0x60, 0x006B, 0x12},
	{0x60, 0x0073, 0x13},
	{0x60, 0x007B, 0x32},
	{0x60, 0x0083, 0x32},
	{0x60, 0x008B, 0x32},
	{0x60, 0x0093, 0x32},
	{0x60, 0x009B, 0x32},
	{0x60, 0x00A3, 0x32},
	{0x60, 0x00AB, 0x32},

	{0x48, 0x0010, 0x23},
	{0x60, 0x0010, 0x21},
	{0x62, 0x0010, 0x21},

	//configure for linkA
	{0x60, 0x0330, 0x00}, // Set SER to 1x4 mode (phy_config = 0)
	//{0x62, 0x0332, 0xE4}, // Verify lane map is at its default (phy1_lane_map = 4'hE, phy2_lane_map = 4'h4 )
	{0x60, 0x0333, 0xE4}, // Additional lane map
	{0x60, 0x0331, 0x30}, // Set 4 lanes for serializer (ctrl1_num_lanes = 3)
	{0x60, 0x0311, 0x30}, // Start video from both port A and port B.
	{0x60, 0x0308, 0x63}, // Enable info lines. Additional start bits for Port A and B. Use data from port B for all pipelines.
	// Serializer Data}type to Video Pipe routing
	{0x60, 0x0314, 0x6c}, // Route 16bit DCG (DT = 0x30) to VIDEO_X (Bit 6 enable)
	{0x60, 0x0316, 0x22}, // Route 12bit RAW (DT = 0x2C) to VIDEO_Y (Bit 6 enable)
	{0x60, 0x0318, 0x22}, // Route EMBEDDED8 to VIDEO_Z (Bit 6 enable)
	{0x60, 0x031A, 0x22}, // Unused VIDEO_U
	// Serializer enab}le video data transmission from serializer to deserializer.
	{0x60, 0x0002, 0x33}, // Make sure all pipelines start transmission (VID_TX_EN_X/Y/Z/U = 1)

	// Change sernor's i2c address from 0x10 to 0x28
	{0x60, 0x0042, 0x50},
	{0x60, 0x0043, 0x20},

	{0x60, 0x02be, 0x90}, // Enable sensor power down pin.
	{0x60, 0x02bf, 0x60}, // Enable sensor reset pin.
	{0x60, 0x03F1, 0x89}, // Output RCLK to senso

	// Serializer MIPI} CSI-2 PHY settings
	{0x62, 0x0330, 0x00}, // Set SER to 1x4 mode (phy_config = 0)
	//{0x62, 0x0332, 0xE4}, // Verify lane map is at its default (phy1_lane_map = 4'hE, phy2_lane_map = 4'h4 )
	{0x62, 0x0333, 0xE4}, // Additional lane map
	{0x62, 0x0331, 0x30}, // Set 4 lanes for serializer (ctrl1_num_lanes = 3)
	{0x62, 0x0311, 0xc0}, // Start video from and port B.
	{0x62, 0x0308, 0x6c}, // Enable info lines. Additional start bits for Port A and B. Use data from port B for all pipelines.
	// Serializer Data}type to Video Pipe routing
	{0x62, 0x0314, 0x22}, // Route 16bit DCG (DT = 0x30) to VIDEO_X (Bit 6 enable)
	{0x62, 0x0316, 0x22}, // Route 12bit RAW (DT = 0x2C) to VIDEO_Y (Bit 6 enable)
	{0x62, 0x0318, 0x6C}, // Route EMBEDDED8 to VIDEO_Z (Bit 6 enable)
	{0x62, 0x031A, 0x22}, // Unused VIDEO_U
	// Serializer enab}le video data transmission from serializer to deserializer.
	{0x62, 0x0002, 0xc3}, // Make sure all pipelines start transmission (VID_TX_EN_X/Y/Z/U = 1)

	// Change sernor's i2c address from 0x10 to 0x2a
	{0x62, 0x0042, 0x54},
	{0x62, 0x0043, 0x20},

	{0x62, 0x02be, 0x90}, // Enable sensor power down pin.
	{0x62, 0x02bf, 0x60}, // Enable sensor reset pin.
	{0x62, 0x03F1, 0x89}, // Output RCLK to senso

	{0x48, 0x0005, 0x00},
	{0x48, 0x0330, 0x04}, // Set MIPI Phy Mode: 2x(1x4) mode
	{0x48, 0x0333, 0x4E}, // lane maps - all 4 ports mapped straight
	{0x48, 0x0334, 0xE4}, // Additional lane map
	{0x48, 0x040A, 0x00}, // lane count - 0 lanes striping on controller 0 (Port A slave in 2x1x4 mode).
	{0x48, 0x044A, 0x40}, // lane count - 2 lanes striping on controller 1 (Port A master in 2x1x4 mode).
	{0x48, 0x048A, 0x40}, // lane count - 2 lanes striping on controller 2 (Port B master in 2x1x4 mode).
	{0x48, 0x04CA, 0x00}, // lane count - 0 lanes striping on controller 3 (Port B slave in 2x1x4 mode).
	// Deserializer MIPI CSI-2 clock rate settings.
	{0x48, 0x031D, 0x2c}, // MIPI clock rate - 1.2Gbps from controller 0 clock (Port A slave in 2x1x4 mode).
	{0x48, 0x0320, 0x2c}, // MIPI clock rate - 1.2Gbps from controller 1 clock (Port A master in 2x1x4 mode).
	{0x48, 0x0323, 0x2c}, // MIPI clock rate - 1.2Gbps from controller 2 clock (Port B master in 2x1x4 mode).
	{0x48, 0x0326, 0x2c}, // MIPI clock rate - 1.2Gbps from controller 2 clock (Port B slave in 2x1x4 mode).
	// Deserializer stream select programming.
	{0x48, 0x0050, 0x00}, // Route data from stream 0 to pipe X
	//{0x48, 0x0051, 0x01}, // Route data from stream 0 to pipe Y MAXIM INTEGRATED CONFIDENTIAL
	{0x48, 0x0052, 0x02}, // Route data from stream 0 to pipe Z
	//{0x48, 0x0053, 0x00}, // Route data from stream 0 to pipe U

	{0x62, 0x02D8, 0x10}, // MFP8 for FSIN
	{0x62, 0x02D6, 0x04},

	{0x60, 0x02D8, 0x10}, // MFP8 for FSIN
	{0x60, 0x02D6, 0x04},

	{0x48, 0x0005, 0x00}, // need disable pixel clk out inb order to use MFP1
	{0x48, 0x02B3, 0x83}, // GPIO TX compensation
	{0x48, 0x02B4, 0x10},

	{0x48, 0x0332, 0xF0}, // Enable all PHYS.
};

#endif

