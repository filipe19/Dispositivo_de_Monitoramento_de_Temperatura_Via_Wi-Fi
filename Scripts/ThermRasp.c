Código para o sensor de temperatura termopar

// include files
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <linux/spi/spidev.h>
#include <unistd.h> // sleep
#include <time.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>

// definições
#define DBG_PRINT 0
#define BCM2708_PERI_BASE        0x20000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */
#define PAGE_SIZE (4*1024)
#define BLOCK_SIZE (4*1024)

#define ADS1118_TS			   (0x0010)    
#define ADS1118_PULLUP     	   (0x0008)
#define ADS1118_NOP     	   (0x0002)  
#define ADS1118_CNVRDY     	   (0x0001)
#define ADSCON_CH0		(0x8B8A)
#define ADSCON_CH1		(0xBB8A)

#define INTERNAL_SENSOR 0
#define EXTERNAL_SIGNAL 1
#define BUFSIZE 64
#define LCD_RS_GPIO 17

#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))
// define bits que são 1 ignora bits que são 0
#define GPIO_SET *(gpio+7)
//define bits que são 1 ignora bits que são 0
#define GPIO_CLR *(gpio+10)
// 0 se BAIXO, (1<<g) se ALTO
#define GET_GPIO(g) (*(gpio+13)&(1<<g))
// Pull up/pull down
#define GPIO_PULL *(gpio+37)
// Pull up/pull down clock
#define GPIO_PULLCLK0 *(gpio+38)

// typedefs
typedef struct spi_ioc_transfer spi_t;

// variáveis globais
int  mem_fd;
void *gpio_map;
volatile unsigned *gpio;
extern int errno;
static const char *device0 = "/dev/spidev0.0";
static const char *device1 = "/dev/spidev0.1";
int ads_fd;
int lcd_fd;
spi_t spi;
int dofile;
FILE* outfile;
int lcd_initialised=0;
uint8_t spi_bits = 8;
//uint32_t spi_speed = 2621440;
uint32_t spi_speed = 3932160;
unsigned char txbuf[BUFSIZE];
unsigned char rxbuf[BUFSIZE];
int local_comp;

// Funções
int
delay_ms(unsigned int msec)
{
  int ret;
  struct timespec a;
  if (msec>999)
  {
    msec=999;
  }
  a.tv_nsec=((long)(msec))*1E6d;
  a.tv_sec=0;
  return(0);
}

// Configura uma região de memória para acesso GPIO
void setup_io()
{
   /* open /dev/mem */
   if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
      printf("can't open /dev/mem \n");
      exit(-1);
   }

   /* mmap GPIO */
   gpio_map = mmap(
      NULL,             //Qualquer endereço no nosso espaço irá fazer
      BLOCK_SIZE,       //Tamanho mapeado
      PROT_READ|PROT_WRITE,//Habilita leitura e escrita para a memória mapeada
      MAP_SHARED,       //Compartilha com outros processos
      mem_fd,           //Arquivo para mapear
      GPIO_BASE         
   );

   close(mem_fd); //Não é necessário manter mem_fd aberto após mmap

   if (gpio_map == MAP_FAILED) {
      printf("mmap error %d\n", (int)gpio_map);
      exit(-1);
   }

   // Sempre usa ponto volátil
   gpio = (volatile unsigned *)gpio_map;
}

int
spi_open(int* f_desc, int sel, uint8_t config)
{
	uint8_t spi_bits = 8;
	int ret;
	
	if (sel)
		*f_desc=open(device1, O_RDWR);
	else
		*f_desc=open(device0, O_RDWR);
	if (*f_desc<0)
	{
		fprintf(stderr, "Error opening device: %s\n", strerror(errno));
		return(-1);
  }
	ret=ioctl(*f_desc, SPI_IOC_WR_MODE, &config);
	if (ret<0)
  {
  	fprintf(stderr, "Error setting SPI write mode: %s\n", strerror(errno));
		return(-1);
  }
	ret=ioctl(*f_desc, SPI_IOC_RD_MODE, &config);
  if (ret<0)
  {
  	fprintf(stderr, "Error setting SPI read mode: %s\n", strerror(errno));
		return(-1);
  }
  ret=ioctl(*f_desc, SPI_IOC_WR_BITS_PER_WORD, &spi_bits);
  if (ret<0)
  {
  	fprintf(stderr, "Error setting SPI write bits: %s\n", strerror(errno));
		return(-1);
  }
  ret=ioctl(*f_desc, SPI_IOC_RD_BITS_PER_WORD, &spi_bits);
  if (ret<0)
  {
  	fprintf(stderr, "Error setting SPI read bits: %s\n", strerror(errno));
		return(-1);
  }
  ret=ioctl(*f_desc, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed);
  if (ret<0)
  {
  	fprintf(stderr, "Error setting SPI write speed: %s\n", strerror(errno));
		return(-1);
  }
  ret=ioctl(*f_desc, SPI_IOC_RD_MAX_SPEED_HZ, &spi_speed);
  if (ret<0)
  {
  	fprintf(stderr, "Error setting SPI read speed: %s\n", strerror(errno));
		return(-1);
  }	
	
	return(0);
}

//escreve comandos para o LCD
void
lcd_writecom(unsigned char c)
{
	int ret;
	
	GPIO_CLR = 1<<LCD_RS_GPIO; //define RS baixo para o comando transmitido

	txbuf[0]=c;
	spi.len=1;
  spi.delay_usecs=0;
  spi.speed_hz=spi_speed;
  spi.bits_per_word=spi_bits;
  spi.cs_change=0;
  spi.tx_buf=(unsigned long)txbuf;
  spi.rx_buf=(unsigned long)rxbuf;

  ret=ioctl(lcd_fd, SPI_IOC_MESSAGE(1), &spi);
  if (ret<0)
  {
  	fprintf(stderr, "Error performing SPI exchange: %s\n", strerror(errno));
		exit(1);
  }
}

void
lcd_writedata(unsigned char c)	//escreve os dados
{
	int ret;
	
	GPIO_SET = 1<<LCD_RS_GPIO; //define RS alto para escrita de dados

	txbuf[0]=c;
	spi.len=1;
  spi.delay_usecs=0;
  spi.speed_hz=spi_speed;
  spi.bits_per_word=spi_bits;
  spi.cs_change=0;
  spi.tx_buf=(unsigned long)txbuf;
  spi.rx_buf=(unsigned long)rxbuf;

  ret=ioctl(lcd_fd, SPI_IOC_MESSAGE(1), &spi);
  if (ret<0)
  {
  	fprintf(stderr, "Error performing SPI exchange: %s\n", strerror(errno));
		exit(1);
  }
}

void
lcd_clear(void)
{
	lcd_writecom(0x01);
	delay_ms(2);
	lcd_writecom(0x02);
	delay_ms(2);
}

void
lcd_display_string(unsigned char line_num, char *ptr) //Escreve uma string na linha do LCD
{

	if(line_num==0)		//primeira linha
	{
		lcd_writecom(0x80);
	}
	else if(line_num==1)	//segunda linha
	{
		lcd_writecom(0xc0);
	}

	while (*ptr)
	{
		lcd_writedata(*ptr++);
	}

}

// inicializa e limpa o display
void
lcd_init(void)
{
	GPIO_SET = 1<<LCD_RS_GPIO;
	lcd_writecom(0x30);	//acorda
	lcd_writecom(0x39);	//define função
	lcd_writecom(0x14);	//oscilador de frequencia interno
	lcd_writecom(0x56);	//controle liga desliga
	lcd_writecom(0x6D);	//controle seguidor
	lcd_writecom(0x70);	//contraste
	lcd_writecom(0x0C);	//display ligado
	lcd_writecom(0x06);	//modo de entrada
	lcd_writecom(0x01);	//limpar
	delay_ms(20);
}

// envia quatro bytes (dois bytes de configuração repetidos uma segunda vez, e retorna dois bytes

int
therm_transact(void)
{
	int ret;
	
	txbuf[0]=txbuf[0] | 0x80;
	
	spi.len=4;
	txbuf[2]=txbuf[0];
	txbuf[3]=txbuf[1];

  spi.delay_usecs=0;
  spi.speed_hz=spi_speed;
  spi.bits_per_word=spi_bits;
  spi.cs_change=0;
  spi.tx_buf=(unsigned long)txbuf;
  spi.rx_buf=(unsigned long)rxbuf;

	if (DBG_PRINT)  
  	printf("sending [%02x %02x %02x %02x]. ", txbuf[0], txbuf[1], txbuf[2], txbuf[3]);

  ret=ioctl(ads_fd, SPI_IOC_MESSAGE(1), &spi);
  if (ret<0)
  {
  	fprintf(stderr, "Error performing SPI exchange: %s\n", strerror(errno));
		exit(1);
  }
  
  if (DBG_PRINT)
  	printf("received [%02x %02x]\n", rxbuf[0], rxbuf[1]);
  ret=rxbuf[0];
  ret=ret<<8;
  ret=ret | rxbuf[1];
  return(ret);
}

Int local_compensation(int local_code) //está função transforma o código do sensor de temperatura interna para o de compensação, no qual ainda é adicionado ao código do termopar
{
	float tmp,local_temp;
	int comp;
	local_code = local_code / 4;
	local_temp = (float)local_code / 32;	

	if (local_temp >=0 && local_temp <=5)		//0~5
	{
		tmp = (0x0019*local_temp)/5;
		comp = tmp;
	}
	else if (local_temp> 5 && local_temp <=10)	//5~10
	{
		tmp = (0x001A*(local_temp - 5))/5 + 0x0019 ;
		comp = tmp;
	}
	else if (local_temp> 10 && local_temp <=20)	//10~20
	{
		tmp = (0x0033*(local_temp - 10))/10 + 0x0033 ;
		comp = tmp;
	}
	else if (local_temp> 20 && local_temp <=30)	//20~30
	{
		tmp = (0x0034*(local_temp - 20))/10 + 0x0066 ;
		comp = tmp;
	}
	else if (local_temp> 30 && local_temp <=40)	//30~40
	{
		tmp = (0x0034*(local_temp - 30))/10 + 0x009A ;
		comp = tmp;
	}
	else if (local_temp> 40 && local_temp <=50)	//40~50
	{
		tmp = (0x0035*(local_temp - 40))/10 + 0x00CE;
		comp = tmp;
	}

	else if (local_temp> 50 && local_temp <=60)	//50~60
	{
		tmp = (0x0035*(local_temp - 50))/10 + 0x0103;
		comp = tmp;
	}
	else if (local_temp> 60 && local_temp <=80)	//60~80
	{
		tmp = (0x006A*(local_temp - 60))/20 + 0x0138;
		comp = tmp;
	}
	else if (local_temp> 80 && local_temp <=125)//80~125
	{
		tmp = (0x00EE*(local_temp - 80))/45 + 0x01A2;
		comp = tmp;
	}
	else
	{
		comp = 0;
	}
	return comp;
}

//está função é usada para converter resultados do conversor AD para temperatura
Int adc_code2temp(int code)	
{
	float temp;
	int t;

	temp = (float)code;


	if (code > 0xFCC6 && code < 0xFCC8) temp = (float)(10*(temp-0xFCC6)) / 0x0002 -270.0f;  // -270 C to -260 C
	else if (code > 0xFCC8 && code < 0xFCCD) temp = (float)(10*(temp-0xFCC8)) / 0x0004 -260.0f;  // -260 C to -250 C
	else if (code > 0xFCCD && code < 0xFCD4) temp = (float)(10*(temp-0xFCCD)) / 0x0007 -250.0f;  // -250 C to -240 C
	else if (code > 0xFCD4 && code < 0xFCDF) temp = (float)(10*(temp-0xFCD4)) / 0x000A -240.0f;  // -240 C to -230 C
	else if (code > 0xFCDF && code < 0xFCEC) temp = (float)(10*(temp-0xFCDF)) / 0x000D -230.0f;  // -230 C to -220 C
	else if (code > 0xFCEC && code < 0xFCFC) temp = (float)(10*(temp-0xFCEC)) / 0x000F -220.0f;  // -220 C to -210 C
	else if (code > 0xFCFC && code < 0xFD0E) temp = (float)(10*(temp-0xFCFC)) / 0x0012 -210.0f;  // -210 C to -200 C
	else if (code > 0xFD0E && code < 0xFD23) temp = (float)(10*(temp-0xFD0E)) / 0x0014 -200.0f;  // -200 C to -190 C
	else if (code > 0xFD23 && code < 0xFD3A) temp = (float)(10*(temp-0xFD23)) / 0x0017 -190.0f;  // -190 C to -180 C
	else if (code > 0xFD3A && code < 0xFD53) temp = (float)(10*(temp-0xFD3A)) / 0x0019 -180.0f;  // -180 C to -170 C
	else if (code > 0xFD53 && code < 0xFD6E) temp = (float)(10*(temp-0xFD53)) / 0x001B -170.0f;  // -170 C to -160 C
	else if (code > 0xFD6E && code < 0xFD8C) temp = (float)(10*(temp-0xFD6E)) / 0x001D -160.0f;  // -160 C to -150 C
	else if (code > 0xFD8C && code < 0xFDAB) temp = (float)(10*(temp-0xFD8C)) / 0x001F -150.0f;  // -150 C to -140 C
	else if (code > 0xFDAB && code < 0xFDCC) temp = (float)(10*(temp-0xFDAB)) / 0x0021 -140.0f;  // -140 C to -130 C
	else if (code > 0xFDCC && code < 0xFDEF) temp = (float)(10*(temp-0xFDCC)) / 0x0022 -130.0f;  // -130 C to -120 C
	else if (code > 0xFDEF && code < 0xFE13) temp = (float)(10*(temp-0xFDEF)) / 0x0024 -120.0f;  // -120 C to -110 C
	else if (code > 0xFE13 && code < 0xFE3A) temp = (float)(10*(temp-0xFE13)) / 0x0026 -110.0f;  // -110 C to -100 C
	else if (code > 0xFE3A && code < 0xFE61) temp = (float)(10*(temp-0xFE3A)) / 0x0027 -100.0f;  // -100 C to  -90 C
	else if (code > 0xFE61 && code < 0xFE8B) temp = (float)(10*(temp-0xFE61)) / 0x0029 -90.0f;  //  -90 C to  -80 C
	else if (code > 0xFE8B && code < 0xFEB5) temp = (float)(10*(temp-0xFE8B)) / 0x002A -80.0f;  //  -80 C to  -70 C
	else if (code > 0xFEB5 && code < 0xFEE1) temp = (float)(10*(temp-0xFEB5)) / 0x002C -70.0f;  //  -70 C to  -60 C
	else if (code > 0xFEE1 && code < 0xFF0F) temp = (float)(10*(temp-0xFEE1)) / 0x002D -60.0f;  //  -60 C to  -50 C
	else if (code > 0xFF0F && code < 0xFF3D) temp = (float)(10*(temp-0xFF0F)) / 0x002E -50.0f;  //  -50 C to  -40 C
	else if (code > 0xFF3D && code < 0xFF6D) temp = (float)(10*(temp-0xFF3D)) / 0x002F -40.0f;  //  -40 C to  -30 C
	else if (code > 0xFF6D && code < 0xFF9D) temp = (float)(10*(temp-0xFF6D)) / 0x0030 -30.0f;  //  -30 C to  -20 C
	else if (code > 0xFF9D && code < 0xFFCE) temp = (float)(10*(temp-0xFF9D)) / 0x0031 -20.0f;  //  -20 C to  -10 C
	else if (code > 0xFFCE && code < 0x0000) temp = (float)(10*(temp-0xFFCE)) / 0x0032 -10.0f;  //  -10 C to    0 C
	else if (code > 0x0000 && code < 0x0032) temp = (float)(10*(temp-0x0000)) / 0x0032 + 0.0f;  //    0 C to   10 C
	else if (code > 0x0032 && code < 0x0066) temp = (float)(10*(temp-0x0032)) / 0x0033 + 10.0f;  //   10 C to   20 C
	else if (code > 0x0066 && code < 0x0099) temp = (float)(10*(temp-0x0066)) / 0x0033 + 20.0f;  //   20 C to   30 C
	else if (code > 0x0099 && code < 0x00CE) temp = (float)(10*(temp-0x0099)) / 0x0034 + 30.0f;  //   30 C to   40 C
	else if (code > 0x00CE && code < 0x0102) temp = (float)(10*(temp-0x00CE)) / 0x0034 + 40.0f;  //   40 C to   50 C
	else if (code > 0x0102 && code < 0x0137) temp = (float)(10*(temp-0x0102)) / 0x0034 + 50.0f;  //   50 C to   60 C
	else if (code > 0x0137 && code < 0x016C) temp = (float)(10*(temp-0x0137)) / 0x0035 + 60.0f;  //   60 C to   70 C
	else if (code > 0x016C && code < 0x01A2) temp = (float)(10*(temp-0x016C)) / 0x0035 + 70.0f;  //   70 C to   80 C
	else if (code > 0x01A2 && code < 0x01D7) temp = (float)(10*(temp-0x01A2)) / 0x0035 + 80.0f;  //   80 C to   90 C
	else if (code > 0x01D7 && code < 0x020C) temp = (float)(10*(temp-0x01D7)) / 0x0034 + 90.0f;  //   90 C to  100 C
	else if (code > 0x020C && code < 0x0241) temp = (float)(10*(temp-0x020C)) / 0x0034 + 100.0f;  //  100 C to  110 C
	else if (code > 0x0241 && code < 0x0275) temp = (float)(10*(temp-0x0241)) / 0x0034 + 110.0f;  //  110 C to  120 C
	else if (code > 0x0275 && code < 0x02A9) temp = (float)(10*(temp-0x0275)) / 0x0034 + 120.0f;  //  120 C to  130 C
	else if (code > 0x02A9 && code < 0x02DE) temp = (float)(10*(temp-0x02A9)) / 0x0034 + 130.0f;  //  130 C to  140 C
	else if (code > 0x02DE && code < 0x0311) temp = (float)(10*(temp-0x02DE)) / 0x0033 + 140.0f;  //  140 C to  150 C
	else if (code > 0x0311 && code < 0x0345) temp = (float)(10*(temp-0x0311)) / 0x0033 + 150.0f;  //  150 C to  160 C
	else if (code > 0x0345 && code < 0x0378) temp = (float)(10*(temp-0x0345)) / 0x0033 + 160.0f;  //  160 C to  170 C
	else if (code > 0x0378 && code < 0x03AB) temp = (float)(10*(temp-0x0378)) / 0x0033 + 170.0f;  //  170 C to  180 C
	else if (code > 0x03AB && code < 0x03DE) temp = (float)(10*(temp-0x03AB)) / 0x0033 + 180.0f;  //  180 C to  190 C
	else if (code > 0x03DE && code < 0x0411) temp = (float)(10*(temp-0x03DE)) / 0x0033 + 190.0f;  //  190 C to  200 C
	else if (code > 0x0411 && code < 0x0444) temp = (float)(10*(temp-0x0411)) / 0x0033 + 200.0f;  //  200 C to  210 C
	else if (code > 0x0444 && code < 0x0478) temp = (float)(10*(temp-0x0444)) / 0x0033 + 210.0f;  //  210 C to  220 C
	else if (code > 0x0478 && code < 0x04AB) temp = (float)(10*(temp-0x0478)) / 0x0033 + 220.0f;  //  220 C to  230 C
	else if (code > 0x04AB && code < 0x04DF) temp = (float)(10*(temp-0x04AB)) / 0x0033 + 230.0f;  //  230 C to  240 C
	else if (code > 0x04DF && code < 0x0513) temp = (float)(10*(temp-0x04DF)) / 0x0033 + 240.0f;  //  240 C to  250 C
	else if (code > 0x0513 && code < 0x0547) temp = (float)(10*(temp-0x0513)) / 0x0034 + 250.0f;  //  250 C to  260 C
	else if (code > 0x0547 && code < 0x057C) temp = (float)(10*(temp-0x0547)) / 0x0034 + 260.0f;  //  260 C to  270 C
	else if (code > 0x057C && code < 0x05B0) temp = (float)(10*(temp-0x057C)) / 0x0034 + 270.0f;  //  270 C to  280 C
	else if (code > 0x05B0 && code < 0x05E5) temp = (float)(10*(temp-0x05B0)) / 0x0034 + 280.0f;  //  280 C to  290 C
	else if (code > 0x05E5 && code < 0x061A) temp = (float)(10*(temp-0x05E5)) / 0x0034 + 290.0f;  //  290 C to  300 C
	else if (code > 0x061A && code < 0x064F) temp = (float)(10*(temp-0x061A)) / 0x0035 + 300.0f;  //  300 C to  310 C
	else if (code > 0x064F && code < 0x0685) temp = (float)(10*(temp-0x064F)) / 0x0035 + 310.0f;  //  310 C to  320 C
	else if (code > 0x0685 && code < 0x06BA) temp = (float)(10*(temp-0x0685)) / 0x0035 + 320.0f;  //  320 C to  330 C
	else if (code > 0x06BA && code < 0x06EF) temp = (float)(10*(temp-0x06BA)) / 0x0035 + 330.0f;  //  330 C to  340 C
	else if (code > 0x06EF && code < 0x0725) temp = (float)(10*(temp-0x06EF)) / 0x0035 + 340.0f;  //  340 C to  350 C
	else if (code > 0x0725 && code < 0x075B) temp = (float)(10*(temp-0x0725)) / 0x0035 + 350.0f;  //  350 C to  360 C
	else if (code > 0x075B && code < 0x0791) temp = (float)(10*(temp-0x075B)) / 0x0035 + 360.0f;  //  360 C to  370 C
	else if (code > 0x0791 && code < 0x07C6) temp = (float)(10*(temp-0x0791)) / 0x0035 + 370.0f;  //  370 C to  380 C
	else if (code > 0x07C6 && code < 0x07FC) temp = (float)(10*(temp-0x07C6)) / 0x0035 + 380.0f;  //  380 C to  390 C
	else if (code > 0x07FC && code < 0x0832) temp = (float)(10*(temp-0x07FC)) / 0x0036 + 390.0f;  //  390 C to  400 C
	else if (code > 0x0832 && code < 0x0868) temp = (float)(10*(temp-0x0832)) / 0x0036 + 400.0f;  //  400 C to  410 C
	else if (code > 0x0868 && code < 0x089F) temp = (float)(10*(temp-0x0868)) / 0x0036 + 410.0f;  //  410 C to  420 C
	else if (code > 0x089F && code < 0x08D5) temp = (float)(10*(temp-0x089F)) / 0x0036 + 420.0f;  //  420 C to  430 C
	else if (code > 0x08D5 && code < 0x090B) temp = (float)(10*(temp-0x08D5)) / 0x0036 + 430.0f;  //  430 C to  440 C
	else if (code > 0x090B && code < 0x0942) temp = (float)(10*(temp-0x090B)) / 0x0036 + 440.0f;  //  440 C to  450 C
	else if (code > 0x0942 && code < 0x0978) temp = (float)(10*(temp-0x0942)) / 0x0036 + 450.0f;  //  450 C to  460 C
	else if (code > 0x0978 && code < 0x09AE) temp = (float)(10*(temp-0x0978)) / 0x0036 + 460.0f;  //  460 C to  470 C
	else if (code > 0x09AE && code < 0x09E5) temp = (float)(10*(temp-0x09AE)) / 0x0036 + 470.0f;  //  470 C to  480 C
	else if (code > 0x09E5 && code < 0x0A1B) temp = (float)(10*(temp-0x09E5)) / 0x0036 + 480.0f;  //  480 C to  490 C
	else if (code > 0x0A1B && code < 0x0A52) temp = (float)(10*(temp-0x0A1B)) / 0x0036 + 490.0f;  //  490 C to  500 C

       else
        return (int)(10*0x270F);

	t = (int)(10*temp);

	return t;
}

//configura e inicia a conversão
void
ads_config(unsigned int mode, unsigned int chan)
{

	unsigned int tmp;
	int ret;
	
	if(chan)
	{
		if (mode==EXTERNAL_SIGNAL)	
			tmp = ADSCON_CH1;
		else
			tmp = ADSCON_CH1 + ADS1118_TS;
	}
	else
	{
		if (mode==EXTERNAL_SIGNAL)	
			tmp = ADSCON_CH0;
		else
			tmp = ADSCON_CH0 + ADS1118_TS;
	}

	txbuf[0]=(unsigned char)((tmp>>8) & 0xff);
	txbuf[1]=(unsigned char)(tmp & 0xff);
	ret=therm_transact();
}

//ler o resultado da conversão AD e inicia uma nova conversão
int
ads_read(unsigned int mode, unsigned int chan)
{
	unsigned int tmp;
	int result;

	if(chan)
	{
		if (mode==EXTERNAL_SIGNAL)	
			tmp = ADSCON_CH1;
		else
			tmp = ADSCON_CH1 + ADS1118_TS;
	}
	else
	{
		if (mode==EXTERNAL_SIGNAL)	
			tmp = ADSCON_CH0;
		else
			tmp = ADSCON_CH0 + ADS1118_TS;
	}


	txbuf[0]=(unsigned char)((tmp>>8) & 0xff);
	txbuf[1]=(unsigned char)(tmp & 0xff);
	result=therm_transact();

	return(result);
}

// retorna a temperatura medida
double
get_measurement(void)
{
	int result;
	int local_data;
	double result_d;
	
	ads_config(INTERNAL_SENSOR,0);  //inicia a medida do sensor de temperatura interna
	delay_ms(10);
	local_data=ads_read(EXTERNAL_SIGNAL,0); // ler a medição do sensor de temperatura interna e inicia o sensor de temperatura externa
	delay_ms(10);
	result=ads_read(EXTERNAL_SIGNAL,0); //ler a medição do sensor de temperatura externa e reinicia o sensor externo	
	local_comp = local_compensation(local_data);
	result = result + local_comp;
	result=result & 0xffff;
	result = adc_code2temp(result);
	result_d=((double)result)/10;
	return(result_d);
}

double
get_measurement_fast(void)
{
	int result;
	double result_d;
	
	result=ads_read(EXTERNAL_SIGNAL,0); //ler a medição do sensor de temperatura externa a reinicia o sensort
	result = result + local_comp;
	result=result & 0xffff;
	result = adc_code2temp(result);
	result_d=((double)result)/10;
	return(result_d);
}

void
unixtime2string(char* int_part, char* out_time)
{
	unsigned int nutime;
	struct tm *nts;
	char buf1[100];
	char buf2[50];
	
	sscanf(int_part, "%u", &nutime);
	nts=localtime((time_t*)&nutime);
	strftime(buf1, 100, "%H:%M:%S", nts);
	
	strcpy(out_time, buf1);	
}

void sig_handler(int signo)
{
  if (signo == SIGINT)
    printf("received SIGINT\n");
  if (dofile)
  {
  	fclose(outfile);
  }
  close(ads_fd);
  lcd_clear();
	lcd_display_string(1,"Bye");
  close(lcd_fd);
  printf("Bye\n");
  exit(0);
}

int
main(int argc, char* argv[])
{
	int ret;
	int i;
	uint8_t spi_config=0;
	double tval;
	int repeat=0;
	int period=1;
	char fname[128];
	char tstring[128];
	char tstring2[128];
	time_t mytime;
	time_t desiredtime;
	struct timespec tstime;
	int not_finished=1;
	int elapsed=0;
	int showtime=0;
	
	// inicializa o GPIO
	setup_io();
	INP_GPIO(LCD_RS_GPIO); // precisa usar primeiro INP_GPIO e depois OUT_GPIO
  OUT_GPIO(LCD_RS_GPIO);
	
	dofile=0;
	if (argc>2)
	{
		if (strcmp(argv[1], "msg")==0) //imprime no LCD
		{
			spi_config=0;
			ret=spi_open(&lcd_fd, 0, spi_config);
			if (ret!=0)
			{
				printf("Exiting\n");
			exit(1);
			}
			lcd_display_string(0,argv[2]);
			close(lcd_fd);
			exit(0);
		}
	}
	if (argc>1)
	{
		if (strcmp(argv[1], "-h")==0) //imprime ajuda
		{
			printf("%s [sec] [filename]\n", argv[0]);
			printf("%s msg <message in quotes>\n", argv[0]);
			exit(0);
		}
		if (strcmp(argv[1], "lcdinit")==0) //inicializa o display LCD
		{
			spi_config=0;
			ret=spi_open(&lcd_fd, 0, spi_config);
			if (ret!=0)
			{
				printf("Exiting\n");
				exit(1);
			}
			lcd_init();
			close(lcd_fd);
			exit(0);
		}
		if (strcmp(argv[1], "withtime")==0)
		{
			showtime=1;
		}
		else
		{
			sscanf(argv[1], "%d", &period); // periodo entre medidas
			repeat=1;
		}
		if (argc>3)
		{
			if (strcmp(argv[2], "msg")==0) //imprime no LCD
			{
				spi_config=0;
				ret=spi_open(&lcd_fd, 0, spi_config);
				if (ret!=0)
				{
					printf("Exiting\n");
					exit(1);
				}
				lcd_init();
				lcd_display_string(0,argv[3]);
				lcd_initialised=1;
			}
		}
	}
	if (argc>2)
	{
		strcpy(fname, argv[2]);
		dofile=1;
		if (argc>4)
		{
			if (strcmp(argv[3], "msg")==0) 
			{
				spi_config=0;
				ret=spi_open(&lcd_fd, 0, spi_config);
				if (ret!=0)
				{
					printf("Exiting\n");
					exit(1);
				}
				lcd_init();
				lcd_display_string(0,argv[4]);
				lcd_initialised=1;
			}
		}
	}
	
	if (dofile)
	{
		outfile=fopen(fname, "w");
	}
	
	//abre o SPI para o ADS1118
	spi_config |= SPI_CPHA;
	ret=spi_open(&ads_fd, 1, spi_config);
	if (ret!=0) // SPI erro
	{
		printf("Exiting\n");
		exit(1);
	}

	if (repeat==0)
	{
		//mostra uma única medição e sai
		tval=get_measurement();
		if (showtime)
		{
			mytime = time(NULL);
			sprintf(tstring, "%d", mytime);
			unixtime2string(tstring, tstring2);
			printf("%s %#.1f\n", tstring2, tval);
		}
		else
		{
			printf("%#.1f\n", tval);
		}
		close(ads_fd);
		exit(0);
	}
	
	//abre SPI para o LCD
  spi_config=0;
	ret=spi_open(&lcd_fd, 0, spi_config);
	if (ret!=0)
	{
		printf("Exiting\n");
		exit(1);
	}

	if (lcd_initialised==0)
		lcd_init();
	
	if (dofile)
	{
		//linha 1 do arquivo de saída vai conter as três colunas de descrição
		fprintf(outfile, "Time HH:MM:SS,Elapsed Sec,Temp C\n");
	}
	
	//Alinhar em um número inteiro de segundos e obter o horário atual
  mytime = time(NULL);
  tstime.tv_sec=mytime+1;
  tstime.tv_nsec=0;
  clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &tstime, NULL);
  mytime++;
  desiredtime=mytime;

	signal(SIGINT, sig_handler);
	
	while(not_finished)
	{
		tval=get_measurement();
		for (i=1; i<10; i++)
		{
			delay_ms(10);
			tval=tval+get_measurement_fast();
		}
		tval=tval/10;
		
		//imprime o tempo, contador de medidas e temperaturas
		sprintf(tstring, "%d", mytime);
		unixtime2string(tstring, tstring2);
		if (mytime==desiredtime)
		{
			printf("%s %d %#.1f\n", tstring2, elapsed, tval);
			if (dofile)
			{
				fprintf(outfile, "%s,%d,%#.1f\n", tstring2, elapsed, tval);
				fflush(outfile);
			}
			desiredtime=desiredtime+period;
		}
		
		lcd_clear();
        sprintf(tstring, "%7.1f", tval);
		lcd_display_string(1, tstring);
		// aguarda por um período de tempo
		mytime++;
		tstime.tv_sec=mytime;
		elapsed++;
		clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &tstime, NULL);
	}
	
  close(ads_fd);
    
  // LCD requer polaridade de clock oposta
  spi_config=0;
	ret=spi_open(&lcd_fd, 0, spi_config);
	if (ret!=0)
	{
		printf("Exiting\n");
		exit(1);
	}


	lcd_init();
	lcd_clear();					// limpa o LCD
	lcd_display_string(0,"Hello");
  
  close(lcd_fd);
  
  return(0);
}
