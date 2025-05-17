/*
 * ProjectCode.c
 *
 * Created: 2/25/2025 10:10:48 AM
 * Author : Hamilton Bird 2134305
 * LCD functions
 */ 

//for delay.h
#define F_CPU 4197000//had to mess around to get the right clock value (really inaccurate, supposed to be 4MHz)

//for initLCD
#define INCREMENT 0x06
#define DECREMENT 0x04

//for uart
#define UBRR 51//12 for 38400 baud on U2X or 51 for 9600
#define BT_BUFF 13 //Can only accept 11 chars to show on LCD + /r /n

//for printLCD
#define LCD_MAX 16 //LCD is 16 char wide
#define ADRSAME 0 //start writing where LCD left off by not changing the address
#define LINE1 1 //start at beginning of line 1 of LCD
#define LINE2 2 //start at beginning of line 2 of LCD
#define	MIDLINE1 3 //start on the 8th char space of line 1 (so I can put music note in the middle with bars around it)
#define MIDLINE2 4 //start on the 8th char space of line 2 (so I can put music note in the middle with bars around it)
#define AFTRMID1 5 //start on the 10th char space of line 1 (so bar can be put after note name)
#define AFTRMID2 6 //start on the 10th char space of line 1 (so bar can be put after note name)

//for BarLCD
#define BARS 7

//buffer
#define BUFF_SIZE 10

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

//Timer Counter count between interrupts
volatile unsigned long rise1_clks = 0;//store time of first rising edge
volatile unsigned long rise2_clks = 0;//store time of second rising edge
volatile unsigned long clks = 0;//difference of the two above
volatile char last_edge = 0;//either 1 or 0, to differentiate between first and second edge
char done = 0;//to indicate that we got the 2 rising edges and can now do math

//UART storage
volatile char uart_buff[BT_BUFF];
volatile unsigned int uart_head = 0;
volatile unsigned int uart_tail = 0;
volatile unsigned int uart_cnt = 0;

/*	-Lower 2 bits are b1=E, b0=RS while upper nibble goes directly to upper LCD other bits are unused
	-E is falling edge triggered, and RS=0 for cmd mode and RS=1 for data mode. R/W is wired ground 
	-High nibble comes first then low nibble */
void BarLCD(int);
void printLCD(char [],char);
void initLCD();
void funcset8bit();
void displayON();
void clearLCD();
void entryModeSet(char);
void float2str2UART(double);
unsigned long freqcalc();
void init_ints();
void init_timer();
void num2notestr(); //C to B = 1 to 12
void UARTinit(unsigned int);
void UART_Tx(char);
void UARTstrTx(char []);

int main(void)
{
	unsigned int uart_buff_index = 0;//keep track of where we are in ring buffer
	unsigned int buff_index=0;
	int stable = 0; //to indicate if we're the note changed or stayed the same
	double period = (double)1/F_CPU;//clock period
	double lastsent = 0.0;//keeps last sent frequency to know if it changed or not
	double freq = 0.0;//where we put frequency as a double
	unsigned long cnt=0;//to keep count of clock cycles per period of guitar signal
	unsigned long clk_buff[BUFF_SIZE] = {0};//to keep a couple of past cnt values for checking if note is changing
	int i=0,u=0;//counting variables, mainly used for going through arrays but used all over main
	int notediff = 0;//stores difference between current and closest note
	char uart_buff_cp[BT_BUFF];//array to store incoming uart so it doesn't get lost
	for(i=0;i<BT_BUFF;i++)
	{//initializing to 0
		uart_buff_cp[i] = 0;
		uart_buff[i] = 0;
	}
	
	
	char notesltr[12][3] = {//null terminated note names as characters to go to LCD
		'C',' ',NULL,
		'C','#',NULL,
		'D',' ',NULL,
		'D','#',NULL,
		'E',' ',NULL,
		'F',' ',NULL,
		'F','#',NULL,
		'G',' ',NULL,
		'G','#',NULL,
		'A',' ',NULL,
		'A','#',NULL,
		'B',' ',NULL
						};
	float notesfreq[12] = {//corresponding frequency to the above notes
		65.41,
		69.30,
		73.42,
		77.78,
		82.41,
		87.31,
		92.50,
		98,
		103.83,
		110,
		116.54,
		123.47
						};

	unsigned long delta;//to store difference between current freq and note freqs
	unsigned long small=65000;//to store smallest delta so far
	int ib=0,ub=0;// to store best i and u so far to know where in the array it is.
	unsigned long noteclks[12][3];//to
	
	for( u = 0; u < 3; u++ )
	{//I only typed out the note frequencies for 1 octave, this puts in the rest for the other 2 octaves
		for( i = 0; i < 12; i++ )
		{
			noteclks[i][u] = (unsigned long)(F_CPU/(notesfreq[i]*(1<<u)));//converts to clocks per period of guitar for faster processing
		}
	}
	
	DDRE = 0x3;//for LCD timing and triggering since I omitted the databus
	DDRA = 0xFF;//to send ascii character 8-bits at a time
	PORTA = 0x00;
	
	cli();//make sure interrupts are off for initializations
	
	init_ints();//interrupt init
	init_timer();//timer counter init
	UARTinit(UBRR);
	initLCD();
	
	sei();//turn on interrupts
	
	while(1)//enter main loop
	{
		cnt = 0;
		cnt =  freqcalc();//figure out how many clocks per period of incoming signal
		
		if( cnt < 14990)//taking the average past a certain threshold made things smoother
		{//I'm not really sure why though
			for(i=0; i<9; i++)//take average
			{
				cnt = cnt + freqcalc();
			}
			cnt=(cnt/10);
		}
	
		for(u=0;u<3;u++)//iterate through the octaves
		{
			for (i=0;i<12;i++)//iterate through the notes
			{
				delta = abs( noteclks[i][u] - cnt );//compare note clocks to current signal clocks
				if( small > delta )//if we found a smaller difference
				{
					small = delta;//set the new smallest difference
					ib = i;//store the location of this closest note
					ub = u;//store the location of this closest octave
				}
			}
		}
		small = 65000;//reset high instead of 0 because if we make it 0, nothing will ever get closer than that
		printLCD(notesltr[ib],MIDLINE2);//put current note name in middle of line 2
		
		notediff = (int)lround(168*(log((double)noteclks[ib][ub]/cnt)/log(2)));//calculate how many bars there should be
		BarLCD(notediff);//spit out the bars
		
		clk_buff[buff_index] = cnt; //store clock count 
		buff_index = (buff_index + 1) % BUFF_SIZE; //cycle over
		
		stable = 1;
		for(i=1;i<BUFF_SIZE;i++)
		{
			if (abs((int)(clk_buff[i] - clk_buff[i-1]))>1000)
			{//make sure the count is stable
				stable = 0;
				break;
			}
		}
		
		if(stable == 1)
		{//check if the note is continuous
			freq = 1/(((double)cnt)*period);//turn it into float
			
			if(fabs(freq - lastsent)>0.1)
			{//check if it's different than the last thing we sent to avoid sending repeats
				float2str2UART(freq);//convert to string and sned to UART
				lastsent = freq;//store what we just sent to compare next time
			}
		}
		
			cli();
			i = 0;
			uart_buff_index = uart_tail;
			while ( (uart_buff[uart_buff_index] != 0x00) && (i < BT_BUFF) )
			{
				uart_buff_cp[i] = uart_buff[uart_buff_index];
				uart_buff_index = (uart_buff_index + 1) % BT_BUFF;
				i++;
			}
			uart_cnt = 0;
			uart_buff_cp[i] = 0x00;
			printLCD("                ",LINE1);//clear line 1
			printLCD("User:",LINE1);
			printLCD(uart_buff_cp,ADRSAME);//put in the current users name on LCD
			for(i=0;i<BT_BUFF;i++)
			{//clear the buffer
				uart_buff_cp[i] = 0x00;
			}
			sei();
	}

	
}

ISR(USART_RX_vect)
{
	char received = UDR; // take in the Rx byte
	
	if(received == 0x0D || received == 0x0A) // if Enter key
	{
		if (uart_cnt > 0)
		{
			uart_buff[uart_head] = 0x00; // Null-terminate string
			uart_tail = (uart_head + BT_BUFF - uart_cnt) % BT_BUFF; // Start of the message
		}
		uart_cnt = 0;
		return;
	}
	
	uart_buff[uart_head] = received;
	uart_head = (uart_head + 1) % BT_BUFF;

	if(uart_cnt < BT_BUFF)
	{
		uart_cnt++;
	}
	else
	{
		uart_tail = (uart_tail + 1) % BT_BUFF; // Overwrite oldest
	}
}
ISR(INT0_vect)
{
	if( last_edge == 0)
	{
		//first edge detected
		rise1_clks = TCNT1; //Timer counter value gets put in
		last_edge = 1;
	}
	else
	{
		//second edge detected 
		rise2_clks = TCNT1; //timer counter value gets put in
		last_edge = 0; //we got our second edge so reset it back again
		
		if(rise2_clks < rise1_clks)
			rise2_clks += 65536; //account for overflow 
		
		clks = rise2_clks - rise1_clks;//difference in TCNT gives our count
		last_edge = 0;
		done = 1;//to know we got 2 rising edges
	}
}

void init_timer()
{
	TCCR1B = 0x01; //no pre-scaling so counts on every clock cycle
}

void init_ints()
{
	DDRD = 0x00;
	GICR = 0x40;//only turn on int0
	MCUCR = 0x03;//trigger on rising edge
/*
Side Note:
Trigger on rising edge and not both edges so that duty cycle doesn't matter
(even though it would be faster, it doesn't work)
*/
}
void UARTinit(unsigned int ubrr)
{
	UBRRH = 0x00;
	UBRRL = UBRR;
	
	UCSRA = 0x02; //set U2X high for double
	UCSRB = 0x98; // enable Rx interrupt, Rx and Tx
	UCSRC = 0x86;//8bit mode no parity 1 stop bit
	
}
void UART_Tx(char data)
{
	do
	{
		
	}while (!(UCSRA & 0x20));//wait for empty buff
	UDR = data;//put data in buff to be sent
}

void UARTstrTx(char msg[])
{//send out a string \n terminated for Linux reasons since it is being sent to a pi
	int i=0;
	do 
	{
		UART_Tx(msg[i]);
		i++;
	} while ((i < LCD_MAX) && (msg[i-1] != 0x0A));
}

void BarLCD(int numbar)
/*
Takes a number from -7 to 7 and writes a corresponding amount of bars left or right of the
center 2 chars of the 16 char wide LCD display (on the second line)
*/
{
	char i = 0;
	
	if (numbar < 0)
	{//if negative bar goes to the left
		
		printLCD("       ",AFTRMID2);
		
		numbar = numbar + 7;
		
		PORTE = 0x0;	//Enable pin and Register Select to low
		PORTA = 0xC0;	
		PORTE = 0x2;	//pull Enable high to setup for falling edge for Enable to trigger on PORTC output
		PORTE = 0x0;	//bring Enable low again to create falling edge
		
		_delay_us(220);		//wait to avoid sending chars before LCD is ready
		
		while ( (i < numbar) && (i < BARS) && (numbar != 0) )
		{
			PORTE = 0x1;	//Register select in data mode with Enable low
			PORTA = ' ';	
			PORTE = 0x3;	//pull Enable high for to setup for falling edge
			PORTE = 0x0;	// create falling edge to make LCD take in PORTC
			_delay_us(37);
			i++;
		}
		
		while ( (i < BARS) )//emptying out the rest of the line
		{
			PORTE = 0x1;	//Register select in data mode with Enable low
			PORTA = 0xFF;	
			PORTE = 0x3;	//pull Enable high for to setup for falling edge
			PORTE = 0x0;	// create falling edge to make LCD take in PORTC
			_delay_us(37);
			i++;
		}
		
	}
	else if( numbar == 0 )
	{
		printLCD("       ",AFTRMID2);
		printLCD("       ",LINE2);
	}
	else if( numbar > 0 )
	{//if positive, bar goes to the right
		printLCD("       ",LINE2);
		
		PORTE = 0x0;	//Enable pin and Register Select to low
		PORTA = 0xC9;	// DB7 must be high and rest is 0x09 for the 10th char space on line 2
		PORTE = 0x2;	//pull Enable high to setup for falling edge for Enable to trigger on PORTC output
		PORTE = 0x0;	//bring Enable low again to create falling edge
		
		_delay_us(220);		//wait to avoid sending chars before LCD is ready
	
		while ( (i < numbar) && (i < BARS) && (numbar != 0) ) 
		{
			PORTE = 0x1;	//Register select in data mode with Enable low
			PORTA = 0xFF;	//Put bar character on PORTC
			PORTE = 0x3;	//pull Enable high for to setup for falling edge
			PORTE = 0x0;	// create falling edge to make LCD take in PORTC
			_delay_us(37);
			i++;
		}
	
		while ( (i < BARS) && (numbar != 0) )//emptying out the rest of the line
		{
			PORTE = 0x1;	//Register select in data mode with Enable low
			PORTA = ' ';	//Put SPACE on PORTC
			PORTE = 0x3;	//pull Enable high for to setup for falling edge
			PORTE = 0x0;	// create falling edge to make LCD take in PORTC
			_delay_us(37);
			i++;
		}
	}
}
void printLCD(char msg[],char line)
/*
-First field takes text to be displayed on LCD with limit set by #define LCD_MAX.

-Second field takes char input but I setup defines to use them by name where they are setting
	the DDRAM address in the LCD to decide where the text will start writing. 
*/
{
	char i = 0;
	
	if (line == LINE1)
	{
		PORTE = 0x0;	//Enable pin and Register Select to low
		PORTA = 0x80;	//DB7 must be high for DDRAM address select but rest low for first address
		PORTE = 0x2;	//pull Enable high to setup for falling edge for Enable to trigger on PORTC output
		PORTE = 0x0;	//bring Enable low again to create falling edge
	}
	else if (line == LINE2)
	{
		PORTE = 0x0;	//Enable pin and Register Select to low
		PORTA = 0xC0;	// DB7 must be high but rest is 0x40 for first second line address
		PORTE = 0x2;	//pull Enable high to setup for falling edge for Enable to trigger on PORTC output
		PORTE = 0x0;	//bring Enable low again to create falling edge
	}
	else if (line == ADRSAME)
	{
		//do nothing to maintain same address
	}
	else if (line == MIDLINE1)
	{
		PORTE = 0x0;	//Enable pin and Register Select to low
		PORTA = 0x87;	// DB7 must be high and rest is 0x07 for the 8th char space on line 1
		PORTE = 0x2;	//pull Enable high to setup for falling edge for Enable to trigger on PORTC output
		PORTE = 0x0;	//bring Enable low again to create falling edge
	}
	else if (line == MIDLINE2)
	{
		PORTE = 0x0;	//Enable pin and Register Select to low
		PORTA = 0xC7;	// DB7 must be high and rest is 0x47 for the 8th char space on line 2
		PORTE = 0x2;	//pull Enable high to setup for falling edge for Enable to trigger on PORTC output
		PORTE = 0x0;	//bring Enable low again to create falling edge
	}
	else if (line == AFTRMID1)
	{
		PORTE = 0x0;	//Enable pin and Register Select to low
		PORTA = 0x89;	// DB7 must be high and rest is 0x09 for the 10th char space on line 1
		PORTE = 0x2;	//pull Enable high to setup for falling edge for Enable to trigger on PORTC output
		PORTE = 0x0;	//bring Enable low again to create falling edge
	}
	else if (line == AFTRMID2)
	{
		PORTE = 0x0;	//Enable pin and Register Select to low
		PORTA = 0xC9;	// DB7 must be high and rest is 0x09 for the 10th char space on line 2
		PORTE = 0x2;	//pull Enable high to setup for falling edge for Enable to trigger on PORTC output
		PORTE = 0x0;	//bring Enable low again to create falling edge
	}
	
	_delay_us(200);		//wait to avoid sending chars before LCD is ready
	
	do
	{
		PORTE = 0x1;	//Register select in data mode with Enable low
		PORTA = msg[i];	//put the char of array at current count on PORTC
		PORTE = 0x3;	//pull Enable high for to setup for falling edge
		PORTE = 0x0;	// create falling edge to make LCD take in PORTC 
		_delay_us(37);
		
		i++;
	}while((msg[i] != 0x00) && (i < LCD_MAX) );
	
}

void float2str2UART(double val)
{
	//to convert double to str to send over uart to Pi
	int i=0;
	char str[10];
	dtostrf(val,4,2,str); //put double value into a string of chars
	for(i=0;i<10;i++)
	{
		if( str[i] == 0x00 )
		str[i] =0x0A;
	}
	UARTstrTx(str);
}
unsigned long freqcalc()
{
	unsigned long i=0;
	
	do
	{
		_delay_ms(1);
		i++;
	} while ( (done != 1) && (i < 25) );//prone to some error but not really noticeable
	done = 0;
	
	return clks;//global variable to be able to work with the interrupts
}

/*
Necessary initializations for the LCD transcribed to C from asm on
p.24 Sitronix ST7066U Dot Matrix LCD Controller/Driver datasheet
*/

void initLCD()
{
	_delay_ms(40);	// LCD needs 40ms after power up
	funcset8bit();	// 8bit mode 2 lines and 5x8 font size
	funcset8bit();
	displayON();	// entire display on, cursor on, cursor position on
	clearLCD();		// clear all chars on display
	entryModeSet(INCREMENT);	// cursor moves right, no shifting
}

void funcset8bit()
{
	PORTE = 0x0;
	PORTA = 0x38;
	PORTE = 0x2;
	PORTE = 0x0;
	_delay_us(37);
}
void displayON()
{
	PORTE = 0x0;
	PORTA = 0x0C;
	PORTE = 0x2;
	PORTE = 0x0;
	_delay_us(37);
}
void clearLCD()
{
	PORTE = 0x0;
	PORTA = 0x01;
	PORTE = 0x2;
	PORTE = 0x0;
	_delay_ms(1.52);
}
void entryModeSet(char dir)
{
	PORTE = 0x0;
	PORTA = dir;
	PORTE = 0x2;
	PORTE = 0x0;
	_delay_us(37);
}