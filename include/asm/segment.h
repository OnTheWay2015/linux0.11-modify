
#ifndef _SEGMENT_H_
#define _SEGMENT_H_ 

unsigned char get_fs_byte(const char * addr);
unsigned short get_fs_word(const unsigned short *addr);
unsigned long get_fs_long(const unsigned long *addr);
void put_fs_byte(char val,char *addr);
void put_fs_word(short val,short * addr);
void put_fs_long(unsigned long val,unsigned long * addr);
/*
 * Someone who knows GNU asm better than I should double check the followig.
 * It seems to work, but I don't know if I'm doing something subtly wrong.
 * --- TYT, 11/24/91
 * [ nothing wrong here, Linus ]
 */

unsigned long get_fs(); 
unsigned long get_ds(); 
void set_fs(unsigned long val);
#endif
