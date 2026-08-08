/*
 * oled.c
 *
 *  Created on: Nov 17, 2024
 *      Author: 阿妈粽
 */


#include "oled.h"
#include "tinyml_app.h"

Environment_Data_t g_env_data = {0};

uint8_t CMD_Data[]=
{
	0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,0xA1, 0xC8, 0xDA,

	0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,0x8D, 0x14,

	0xAF
};



/**
 * @function: void OLED_Init(void)
 * @description: OLED初始化
 * @return {*}
 */
void OLED_Init(void)
{
    HAL_Delay(100);

	uint8_t i = 0;
	for(i=0; i<23; i++)
	{
		OLED_WR_CMD(CMD_Data[i]);
	}

}

/**
 * @function: void OLED_WR_CMD(uint8_t cmd)
 * @description: 向设备写控制命令
 * @param {uint8_t} cmd 芯片手册规定的命令
 * @return {*}
 */
void OLED_WR_CMD(uint8_t cmd)
{
	HAL_I2C_Mem_Write(&hi2c1 ,0x78,0x00,I2C_MEMADD_SIZE_8BIT,&cmd,1,0x100);
}

/**
 * @function: void OLED_WR_DATA(uint8_t data)
 * @description: 向设备写控制数据
 * @param {uint8_t} data 数据
 * @return {*}
 */
void OLED_WR_DATA(uint8_t data)
{
	HAL_I2C_Mem_Write(&hi2c1 ,0x78,0x40,I2C_MEMADD_SIZE_8BIT,&data,1,0x100);
}

/**
 * @function: void OLED_On(void)
 * @description: 更新显示

 * @return {*}
 */
void OLED_On(void)
{
	uint8_t i,n;
	for(i=0;i<8;i++)
	{
		OLED_WR_CMD(0xb0+i);    //设置页地址（0~7）
		OLED_WR_CMD(0x00);      //设置显示位置—列低地址
		OLED_WR_CMD(0x10);      //设置显示位置—列高地址
		for(n=0;n<128;n++)
			OLED_WR_DATA(1);
	}
}


/**
 * @function: OLED_Clear(void)
 * @description: 清屏,整个屏幕是黑色的!和没点亮一样!!!
 * @return {*}
 */
void OLED_Clear(void)
{
	uint8_t i,n;
	for(i=0;i<8;i++)
	{
		OLED_WR_CMD(0xb0+i);    //设置页地址（0~7）
		OLED_WR_CMD(0x00);      //设置显示位置—列低地址
		OLED_WR_CMD(0x10);      //设置显示位置—列高地址
		for(n=0;n<128;n++)
			OLED_WR_DATA(0);
	}
}

/**
 * @function: void OLED_Display_On(void)
 * @description: 开启OLED显示
 * @return {*}
 */
void OLED_Display_On(void)
{
	OLED_WR_CMD(0X8D);  //SET DCDC命令
	OLED_WR_CMD(0X14);  //DCDC ON
	OLED_WR_CMD(0XAF);  //DISPLAY ON,打开显示
}


/**
 * @function: void OLED_Display_Off(void)
 * @description: 关闭OLED显示
 * @return {*}
 */
void OLED_Display_Off(void)
{
	OLED_WR_CMD(0X8D);  //SET DCDC命令
	OLED_WR_CMD(0X10);  //DCDC OFF
	OLED_WR_CMD(0XAE);  //DISPLAY OFF，关闭显示
}

/**
 * @function: void OLED_Set_Pos(uint8_t x, uint8_t y)
 * @description: 坐标设置
 * @param {uint8_t} x,y
 * @return {*}
 */
void OLED_Set_Pos(uint8_t x, uint8_t y)
{
	OLED_WR_CMD(0xb0+y);	//设置页地址（0~7）
	OLED_WR_CMD(((x&0xf0)>>4)|0x10); //设置显示位置—列高地址
	OLED_WR_CMD(x&0x0f);	//设置显示位置—列低地址
}


/**
 * @function: unsigned int oled_pow(uint8_t m,uint8_t n)
 * @description: m^n函数
 * @param {uint8_t} m,n
 * @return {unsigned int} result
 */
unsigned int oled_pow(uint8_t m,uint8_t n)
{
	unsigned int result=1;
	while(n--)result*=m;
	return result;
}

/**
 * @function: void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t Char_Size,uint8_t Color_Turn)
 * @description: 在OLED12864特定位置开始显示一个字符
 * @param {uint8_t} x字符开始显示的横坐标
 * @param {uint8_t} y字符开始显示的纵坐标
 * @param {uint8_t} chr待显示的字符
 * @param {uint8_t} Char_Size待显示字符的字体大小,选择字体 16/12
 * @param {uint8_t} Color_Turn是否反相显示(1反相、0不反相)
 * @return {*}
 */
void OLED_ShowChar(uint8_t x,uint8_t y,uint8_t chr,uint8_t Char_Size,uint8_t Color_Turn)
{
	unsigned char c=0,i=0;
		c=chr-' ';//得到偏移后的值
		if(x>128-1){x=0;y=y+2;}
		if(Char_Size ==16)
		{
			OLED_Set_Pos(x,y);
			for(i=0;i<8;i++)
				{
				  if(Color_Turn)
					  OLED_WR_DATA(~F8X16[c*16+i]);
				  else
					  OLED_WR_DATA(F8X16[c*16+i]);
				}
			OLED_Set_Pos(x,y+1);
			for(i=0;i<8;i++)
			    {
				  if(Color_Turn)
					  OLED_WR_DATA(~F8X16[c*16+i+8]);
				  else
					  OLED_WR_DATA(F8X16[c*16+i+8]);
			    }

			}
	     else
	     {
				OLED_Set_Pos(x,y);
				for(i=0;i<6;i++)
			    {
				  if(Color_Turn)
					  OLED_WR_DATA(~F6x8[c][i]);
				  else
					  OLED_WR_DATA(F6x8[c][i]);
			    }
		  }
}

/**
 * @function: void OLED_ShowString(uint8_t x, uint8_t y, uint8_t *chr, uint8_tChar_Size, uint8_t Color_Turn)
 * @description: 在OLED12864特定位置开始显示字符串
 * @param {uint8_t} x待显示字符串的开始横坐标x:0~127
 * @param {uint8_t} y待显示字符串的开始纵坐标 y:0~7，若选择字体大小为16，则两行数字之间需要间隔2，若选择字体大小为12，间隔1
 * @param {uint8_t} *chr待显示的字符串
 * @param {uint8_t} Char_Size待显示字符串的字体大小,选择字体 16/12，16为8X16，12为6x8
 * @param {uint8_t} Color_Turn是否反相显示(1反相、0不反相)
 * @return {*}
 */
void OLED_ShowString(uint8_t x,uint8_t y,char*chr,uint8_t Char_Size, uint8_t Color_Turn)
{
	
	uint8_t  j=0;
//	printf("显示字符: %s\n", chr);
	while (chr[j]!='\0')
	{			
		OLED_ShowChar(x,y,chr[j],Char_Size, Color_Turn);
		if (Char_Size == 12) //6X8的字体列加6，显示下一个字符
			x += 6;
		else  //8X16的字体列加8，显示下一个字符
			x += 8;

		if (x > 122 && Char_Size==12) //TextSize6x8如果一行不够显示了，从下一行继续显示
		{
			x = 0;
			y+=2;
		}
		if (x > 120 && Char_Size== 16) //TextSize8x16如果一行不够显示了，从下一行继续显示
		{
			x = 0;
			y+=2;
			if(y > 6) break; // 防止超出OLED页地址范围(0~7)导致写入无效地址
//			printf("New line at y=%d\n", y);
		}
		j++;
	}  
}

/**
 * @function: void OLED_ShowNum(uint8_t x,uint8_t y,unsigned int num,uint8_t len,uint8_t size2, Color_Turn)
 * @description: 显示数字
 * @param {uint8_t} x待显示的数字起始横坐标,x:0~126
 * @param {uint8_t} y待显示的数字起始纵坐标, y:0~7，若选择字体大小为16，则两行数字之间需要间隔2，若选择字体大小为12，间隔1
 * @param {unsigned int} num:输入的数据
 * @param {uint8_t } len:输入的数据位数
 * @param {uint8_t} size2:输入的数据大小，选择 16/12，16为8X16，12为6x8
 * @param {uint8_t} Color_Turn是否反相显示(1反相、0不反相)
 * @return {*}
 */
void OLED_ShowNum(uint8_t x,uint8_t y,unsigned int num,uint8_t len,uint8_t size2, uint8_t Color_Turn)
{
	uint8_t t,temp;
	uint8_t enshow=0;
	for(t=0;t<len;t++)
	{
		temp=(num/oled_pow(10,len-t-1))%10;
		if(enshow==0&&t<(len-1))
		{
			if(temp==0)
			{
				OLED_ShowChar(x+(size2/2)*t,y,' ',size2, Color_Turn);
				continue;
			}else enshow=1;

		}
	 	OLED_ShowChar(x+(size2/2)*t,y,temp+'0',size2, Color_Turn);
	}
}


/**
 * @function: void OLED_Showdecimal(uint8_t x,uint8_t y,float num,uint8_t z_len,uint8_t f_len,uint8_t size2, uint8_t Color_Turn)
 * @description: 显示正负浮点数
 * @param {uint8_t} x待显示的数字起始横坐标,x:0~126
 * @param {uint8_t} y待显示的数字起始纵坐标, y:0~7，若选择字体大小为16，则两行数字之间需要间隔2，若选择字体大小为12，间隔1
 * @param {float} num:输入的浮点型数据
 * @param {uint8_t } z_ len:整数部分的位数
 * @param {uint8_t } f_len: 小数部分的位数
 * @param {uint8_t} size2:输入的数据大小，选择 16/12，16为8X16，12为6x8
 * @param {uint8_t} Color_Turn是否反相显示(1反相、0不反相)
 * @return {*}
 */
void OLED_Showdecimal(uint8_t x,uint8_t y,float num,uint8_t z_len,uint8_t f_len,uint8_t size2, uint8_t Color_Turn)
{
	uint8_t t,temp,i=0;//i为负数标志位
	uint8_t enshow;
	int z_temp,f_temp;
	if(num<0)
	{
		z_len+=1;
		i=1;
		num=-num;
	}
	z_temp=(int)num;
	//整数部分
	for(t=0;t<z_len;t++)
	{
		temp=(z_temp/oled_pow(10,z_len-t-1))%10;
		if(enshow==0 && t<(z_len-1))
		{
			if(temp==0)
			{
				OLED_ShowChar(x+(size2/2)*t,y,' ',size2, Color_Turn);
				continue;
			}
			else
			enshow=1;
		}
		OLED_ShowChar(x+(size2/2)*t,y,temp+'0',size2, Color_Turn);
	}
	//小数点
	OLED_ShowChar(x+(size2/2)*(z_len),y,'.',size2, Color_Turn);

	f_temp=(int)((num-z_temp)*(oled_pow(10,f_len)));
  //小数部分
	for(t=0;t<f_len;t++)
	{
		temp=(f_temp/oled_pow(10,f_len-t-1))%10;
		OLED_ShowChar(x+(size2/2)*(t+z_len)+5,y,temp+'0',size2, Color_Turn);
	}
	if(i==1)//如果为负，就将最前的一位赋值‘-’
	{
		OLED_ShowChar(x,y,'-',size2, Color_Turn);
		i=0;
	}
}



/**
 * @function: void OLED_ShowCHinese(uint8_t x,uint8_t y,uint8_t no, uint8_t Color_Turn)
 * @description: 在OLED特定位置开始显示16X16汉字
 * @param {uint8_t} x待显示的汉字起始横坐标x: 0~112，两列汉字之间需要间隔16
 * @param {uint8_t} y待显示的汉字起始纵坐标 y: 0~6 , 两行汉字之间需要间隔2
 * @param {uint8_t} no待显示的汉字编号
 * @param {uint8_t} Color_Turn是否反相显示(1反相、0不反相)
 * @return {*}
 */
void OLED_ShowCHinese(uint8_t x,uint8_t y,uint8_t no, uint8_t Color_Turn)
{
	uint8_t t=0;
	OLED_Set_Pos(x,y);
    for(t=0;t<16;t++)
		{
				if (Color_Turn)
					OLED_WR_DATA(~Hzk[2*no][t]); //显示汉字的上半部分
				else
					OLED_WR_DATA(Hzk[2*no][t]); //显示汉字的上半部分
        }

		OLED_Set_Pos(x,y+1);
    for(t=0;t<16;t++)
		{
				if (Color_Turn)
					OLED_WR_DATA(~Hzk[2*no+1][t]); //显示汉字的上半部分
				else
					OLED_WR_DATA(Hzk[2*no+1][t]);//显示汉字的上半部分

         }
}

/**
 * @function: void OLED_DrawBMP(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t *  BMP,uint8_t Color_Turn)
 * @description: 在OLED特定区域显示BMP图片
 * @param {uint8_t} x0图像开始显示横坐标  x0:0~127
 * @param {uint8_t} y0图像开始显示纵坐标  y0:0~7
 * @param {uint8_t} x1图像结束显示横坐标  x1:1~128
 * @param {uint8_t} y1图像结束显示纵坐标  y1:1~8
 * @param {uint8_t} *BMP待显示的图像数据
 * @param {uint8_t} Color_Turn是否反相显示(1反相、0不反相)
 * @return {*}
 */
void OLED_DrawBMP(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t *  BMP,uint8_t Color_Turn)
{
   uint32_t j = 0;
   uint8_t x = 0, y = 0;

  if(y1%8==0)
		y = y1/8;
  else
		y = y1/8 + 1;
	for(y=y0;y<y1;y++)
	{
		OLED_Set_Pos(x0,y);
    for(x=x0;x<x1;x++)
		{
			if (Color_Turn)
				OLED_WR_DATA(~BMP[j++]);//显示反相图片
			else
				OLED_WR_DATA(BMP[j++]);//显示图片

		}
	}
}


/**
 * @function: void OLED_HorizontalShift(uint8_t direction)
 * @description: 屏幕内容水平全屏滚动播放
 * @param {uint8_t} direction			LEFT	   0x27     	RIGHT  0x26
 * @return {*}
 */
void OLED_HorizontalShift(uint8_t direction)

{
	OLED_WR_CMD(0x2e);//停止滚动
	OLED_WR_CMD(direction);//设置滚动方向
	OLED_WR_CMD(0x00);//虚拟字节设置，默认为0x00
	OLED_WR_CMD(0x00);//设置开始页地址
	OLED_WR_CMD(0x07);//设置每个滚动步骤之间的时间间隔的帧频
    //  0x00-5帧， 0x01-64帧， 0x02-128帧， 0x03-256帧， 0x04-3帧， 0x05-4帧， 0x06-25帧， 0x07-2帧，
	OLED_WR_CMD(0x07);//设置结束页地址
	OLED_WR_CMD(0x00);//虚拟字节设置，默认为0x00
	OLED_WR_CMD(0xff);//虚拟字节设置，默认为0xff
	OLED_WR_CMD(0x2f);//开启滚动-0x2f，禁用滚动-0x2e，禁用需要重写数据
}

/**
 * @function: void OLED_Some_HorizontalShift(uint8_t direction,uint8_t start,uint8_t end)
 * @description: 屏幕部分内容水平滚动播放
 * @param {uint8_t} direction			LEFT	   0x27     	RIGHT  0x26
 * @param {uint8_t} start 开始页地址  0x00~0x07
 * @param {uint8_t} end  结束页地址  0x01~0x07
 * @return {*}
 */
void OLED_Some_HorizontalShift(uint8_t direction,uint8_t start,uint8_t end)
{
	OLED_WR_CMD(0x2e);//停止滚动
	OLED_WR_CMD(direction);//设置滚动方向
	OLED_WR_CMD(0x00);//虚拟字节设置，默认为0x00
	OLED_WR_CMD(start);//设置开始页地址
	OLED_WR_CMD(0x07);//设置每个滚动步骤之间的时间间隔的帧频,0x07即滚动速度2帧
	OLED_WR_CMD(end);//设置结束页地址
	OLED_WR_CMD(0x00);//虚拟字节设置，默认为0x00
	OLED_WR_CMD(0xff);//虚拟字节设置，默认为0xff
	OLED_WR_CMD(0x2f);//开启滚动-0x2f，禁用滚动-0x2e，禁用需要重写数据

}

/**
 * @function: void OLED_VerticalAndHorizontalShift(uint8_t direction)
 * @description: 屏幕内容垂直水平全屏滚动播放
 * @param {uint8_t} direction				右上滚动	 0x29
 *                                                            左上滚动   0x2A
 * @return {*}
 */
void OLED_VerticalAndHorizontalShift(uint8_t direction)
{
	OLED_WR_CMD(0x2e);//停止滚动
	OLED_WR_CMD(direction);//设置滚动方向
	OLED_WR_CMD(0x01);//虚拟字节设置
	OLED_WR_CMD(0x00);//设置开始页地址
	OLED_WR_CMD(0x07);//设置每个滚动步骤之间的时间间隔的帧频，即滚动速度
	OLED_WR_CMD(0x07);//设置结束页地址
	OLED_WR_CMD(0x01);//垂直滚动偏移量
	OLED_WR_CMD(0x00);//虚拟字节设置，默认为0x00
	OLED_WR_CMD(0xff);//虚拟字节设置，默认为0xff
	OLED_WR_CMD(0x2f);//开启滚动-0x2f，禁用滚动-0x2e，禁用需要重写数据
}

/**
 * @function: void OLED_DisplayMode(uint8_t mode)
 * @description: 屏幕内容取反显示
 * @param {uint8_t} direction			ON	0xA7  ，
 *                                                          OFF	0xA6	默认此模式，设置像素点亮
 * @return {*}
 */
void OLED_DisplayMode(uint8_t mode)
{
	OLED_WR_CMD(mode);
}

/**
 * @function: void OLED_IntensityControl(uint8_t intensity)
 * @description: 屏幕亮度调节
 * @param  {uint8_t} intensity	0x00~0xFF,RESET=0x7F
 * @return {*}
 */
void OLED_IntensityControl(uint8_t intensity)
{
	OLED_WR_CMD(0x81);
	OLED_WR_CMD(intensity);
}
/**
 * @function: void OLED_ShowCentigrade(uint8_t x, uint8_t y, uint8_t Char_Size, uint8_t Color_Turn)
 * @description: 显示摄氏度符号
 * @param {uint8_t} x 横坐标
 * @param {uint8_t} y 纵坐标
 * @param {uint8_t} Char_Size 字体大小
 * @param {uint8_t} Color_Turn 是否反相
 * @return {*}
 */
void OLED_ShowCentigrade(uint8_t x, uint8_t y, uint8_t Char_Size, uint8_t Color_Turn)
{
    // 摄氏度符号在字体数组中的索引（通常是最后一个）
    uint8_t celsius_index = 94; // 假设摄氏度符号是第95个字符（ASCII扩展）

    if(Char_Size == 16) {
        // 使用8x16字体显示摄氏度
        OLED_Set_Pos(x, y);
        for(uint8_t i=0; i<8; i++) {
            if(Color_Turn)
                OLED_WR_DATA(~F8X16[celsius_index*16 + i]);
            else
                OLED_WR_DATA(F8X16[celsius_index*16 + i]);
        }
        OLED_Set_Pos(x, y+1);
        for(uint8_t i=0; i<8; i++) {
            if(Color_Turn)
                OLED_WR_DATA(~F8X16[celsius_index*16 + i + 8]);
            else
                OLED_WR_DATA(F8X16[celsius_index*16 + i + 8]);
        }
    } else {
        // 使用6x8字体显示摄氏度
        OLED_Set_Pos(x, y);
        for(uint8_t i=0; i<6; i++) {
            if(Color_Turn)
                OLED_WR_DATA(~F6x8[celsius_index][i]);
            else
                OLED_WR_DATA(F6x8[celsius_index][i]);
        }
    }
}

/**
 * @function: void OLED_ShowChineseString(uint8_t x, uint8_t y, uint8_t *chinese, uint8_t len, uint8_t Color_Turn)
 * @description: 显示中文字符串
 * @param {uint8_t} x 起始横坐标
 * @param {uint8_t} y 起始纵坐标
 * @param {uint8_t} *chinese 汉字编号数组
 * @param {uint8_t} len 汉字数量
 * @param {uint8_t} Color_Turn 是否反相
 * @return {*}
 */
void OLED_ShowChineseString(uint8_t x, uint8_t y, uint8_t *chinese, uint8_t len, uint8_t Color_Turn)
{
    for(uint8_t i=0; i<len; i++) {
        OLED_ShowCHinese(x + i*16, y, chinese[i], Color_Turn);
    }
}

/* 显示Cursor[]中的光标符号，每个符号16列×8行(1 page)
 * no: RIGHT1=0("-") / RIGHT2=1(">")  */
void OLED_ShowCursor(uint8_t x, uint8_t y, uint8_t no, uint8_t Color_Turn)
{
    OLED_Set_Pos(x, y);
    for(uint8_t i = 0; i < 16; i++) {
        if(Color_Turn)
            OLED_WR_DATA(~Cursor[no][i]);
        else
            OLED_WR_DATA(Cursor[no][i]);
    }
}


void my_oledprintf(uint8_t Line,char * format , ...)
{
	char formatted[48] = {0};
	char string[17];
	va_list args;
	va_start(args,format);
	vsnprintf(formatted,sizeof(formatted),format,args);
	va_end(args);
	memset(string, ' ', 16);
	strncpy(string, formatted, 16);
	string[16] = '\0';

	char *degree_ptr = strstr(string, "°C");
	if(degree_ptr != NULL)
	{
		uint8_t prefix_len = degree_ptr - string;
		char prefix[8] = {0};
		strncpy(prefix, string, prefix_len);
		prefix[prefix_len] = '\0';
		OLED_ShowString(0, Line, prefix, 16, 0);
		uint8_t degree_x = strlen(prefix) * 8;
		OLED_ShowCentigrade(degree_x, Line, 16, 0);
		OLED_ShowChar(degree_x + 8, Line, 'C', 16, 0);
	}
	else
		OLED_ShowString(0, Line, string, 16, 0);
}

/* 显示中文标签+格式化数值
 * 用法: oled_cnprintf(行, (uint8_t[]){HZ_WEN,HZ_DU2}, 2, "%d °C", temp);
 * 每个汉字16px宽，2个汉字后剩余96px=12个ASCII字符 */
void oled_cnprintf(uint8_t line, const uint8_t *hz, uint8_t hz_cnt, char *format, ...)
{
	// 显示中文标签
	for(uint8_t i = 0; i < hz_cnt; i++)
		OLED_ShowCHinese(i * 16, line, hz[i], 0);

	// 格式化数值，从标签后开始显示
	char string[13] = "            "; // 最多12字符
	va_list args;
	va_start(args, format);
	vsnprintf(string, 12, format, args);
	va_end(args);
	string[12] = '\0';

	uint8_t x_start = hz_cnt * 16;
	char *degree_ptr = strstr(string, "°C");
	if(degree_ptr != NULL)
	{
		uint8_t prefix_len = degree_ptr - string;
		char prefix[8] = {0};
		strncpy(prefix, string, prefix_len);
		prefix[prefix_len] = '\0';
		OLED_ShowString(x_start, line, prefix, 16, 0);
		uint8_t degree_x = x_start + strlen(prefix) * 8;
		OLED_ShowCentigrade(degree_x, line, 16, 0);
		OLED_ShowChar(degree_x + 8, line, 'C', 16, 0);
	}
	else
		OLED_ShowString(x_start, line, string, 16, 0);
}


//==================== VOC 趋势分析器 (环形缓冲区 + 滞回防抖 + 确认计数器) ====================//
#define BUF_SIZE 10 // 10秒环形缓冲区

#if 0
static float  trend_buf[BUF_SIZE] = {0};
static uint8_t trend_buf_idx        = 0;
static uint8_t trend_buf_filled     = 0;
static trend_state_t display_state  = TREND_STABLE; // UI显示状态（防抖后）
static trend_state_t pending_state  = TREND_STABLE; // 候选新状态
static uint8_t state_hold_counter   = 0;            // 状态确认计数器

static const char* trend_labels[] = {
    "Worsen", "Rising", "Stable", "Improve", "Recover"
};

void trend_analyzer_update(float smooth_delta)
{
    // 1. 写入环形缓冲区
    trend_buf[trend_buf_idx] = smooth_delta;
    trend_buf_idx = (trend_buf_idx + 1) % BUF_SIZE;
    if (trend_buf_idx == 0) trend_buf_filled = 1;
    if (!trend_buf_filled) return;

    // 2. 计算ΔY = 当前值 - 10秒前
    float old_val = trend_buf[trend_buf_idx];
    float delta_y = smooth_delta - old_val;

    // 3. 滞回状态机 → 产出候选状态 (pending_state)
    trend_state_t candidate = display_state;
    switch (display_state) {
        case TREND_RAPID_WORSEN:
            if (delta_y <= 15.0f)       candidate = TREND_SLOW_RISE; break;
        case TREND_SLOW_RISE:
            if (delta_y > 25.0f)        candidate = TREND_RAPID_WORSEN;
            else if (delta_y <= 5.0f)   candidate = TREND_STABLE;
            break;
        case TREND_STABLE:
            if (delta_y > 25.0f)        candidate = TREND_RAPID_WORSEN;
            else if (delta_y > 10.0f)   candidate = TREND_SLOW_RISE;
            else if (delta_y < -25.0f)  candidate = TREND_RAPID_RECOVER;
            else if (delta_y < -10.0f)  candidate = TREND_GRADUAL_IMPROVE;
            break;
        case TREND_GRADUAL_IMPROVE:
            if (delta_y < -25.0f)       candidate = TREND_RAPID_RECOVER;
            else if (delta_y >= -5.0f)  candidate = TREND_STABLE;
            break;
        case TREND_RAPID_RECOVER:
            if (delta_y >= -15.0f)      candidate = TREND_GRADUAL_IMPROVE; break;
    }

    // 4. 状态确认防抖：连续2秒同一候选状态才切换UI
    if (candidate == pending_state) {
        state_hold_counter++;
        if (state_hold_counter >= 2) {
            display_state = candidate;
        }
    } else {
        pending_state = candidate;
        state_hold_counter = 1;
    }
}

//==================== 屏幕数据更新函数 ====================//
void lcd_proc(void)
{
char oled_buf[32]; // 局部显示缓冲区

// ----------------- 1. 第一行：温度显示 -----------------
    if(g_env_data.bme680_valid) 
    {
        // %-6.2f 确保数字占用6位宽且靠左，少于6位自动补空格，完美避免因省略OLED_Clear导致的数字残留
        my_oledprintf(0, "Temp: %-6.2f C", g_env_data.temperature);
    } 
    else 
    {
        my_oledprintf(0, "Temp: ERR      "); // 当传感器被拔掉或物理损坏时优雅提示
    }

    // ----------------- 2. 第二行：湿度显示 -----------------
    if(g_env_data.bme680_valid) 
    {
        my_oledprintf(2, "Humi: %-6.2f %%", g_env_data.humidity);
    } 
    else 
    {
        my_oledprintf(2, "Humi: ERR      ");
    }

    // ----------------- 3. 第三行：气压显示 -----------------
    if(g_env_data.bme680_valid) 
    {
        my_oledprintf(4, "Pres: %-6.1f hPa", g_env_data.pressure);
    } 
    else 
    {
        my_oledprintf(4, "Pres: ERR       ");
    }

    // ----------------- 4. 第四行：VOC 趋势预测 -----------------
    if(g_env_data.sgp40_valid)
    {
        my_oledprintf(6, "Trend: %-8s", trend_labels[display_state]);
    }
    else
    {
        my_oledprintf(6, "Trend: ERR     ");
    }
}

/*------------------------------OLED显示----------------------------------------*/
void oled_task()
{
    // 更新显示缓冲区
    lcd_proc();
}
#endif

//==================== 屏幕数据更新函数 ====================//
void lcd_proc(void)
{
    const tinyml_result_t *ai_result = TinyML_App_GetLatestResult();
    uint32_t confidence = (uint32_t)(ai_result->confidence * 100.0f + 0.5f);

    if (confidence > 100U)
    {
        confidence = 100U;
    }

    // ----------------- 1. 第一行：当前30秒TinyML识别结果 -----------------
    switch (ai_result->state)
    {
        case TINYML_STATE_WARMING_UP:
            my_oledprintf(0, "AI:WARMUP %3lus",
                          (unsigned long)TinyML_App_GetWarmupRemainingSeconds());
            break;

        case TINYML_STATE_COLLECTING_WINDOW:
            my_oledprintf(0, "AI:WINDOW %2lu/30",
                          (unsigned long)TinyML_App_GetWindowSamplesCollected());
            break;

        case TINYML_STATE_NORMAL:
            my_oledprintf(0, "AI:NORMAL   %3lu%%", (unsigned long)confidence);
            break;

        case TINYML_STATE_WARNING:
            my_oledprintf(0, "AI:WARNING  %3lu%%", (unsigned long)confidence);
            break;

        case TINYML_STATE_UNCERTAIN:
            my_oledprintf(0, "AI:UNCERT   %3lu%%", (unsigned long)confidence);
            break;

        case TINYML_STATE_ERROR:
        default:
            my_oledprintf(0, "AI:MODEL ERROR");
            break;
    }

    // ----------------- 2. 第二行：未来5分钟模型预留 -----------------
    // 当前模型判断的是最近30秒状态；接入5分钟预测模型后替换“--”。
    my_oledprintf(2, "PRED 5M: --");

    // ----------------- 3. 第三行：VOC数据 -----------------
    if (g_env_data.sgp40_valid)
    {
        my_oledprintf(4, "VOC:%5u I:%3ld",
                      (unsigned int)g_env_data.sgp40_raw,
                      (long)g_env_data.voc_index);
    }
    else
    {
        my_oledprintf(4, "VOC: SENSOR ERR");
    }

    // ----------------- 4. 第四行：温湿度和气压 -----------------
    if (g_env_data.bme680_valid)
    {
        my_oledprintf(6, "T%4.1f H%4.1f P%3.0f",
                      g_env_data.temperature,
                      g_env_data.humidity,
                      g_env_data.pressure);
    }
    else
    {
        my_oledprintf(6, "ENV: SENSOR ERR");
    }
}

/*------------------------------OLED显示----------------------------------------*/
void oled_task()
{
    lcd_proc();
}
