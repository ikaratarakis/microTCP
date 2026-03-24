##########################################
# file: Makefile
#
# @Descr:		Makefile for CS-435 Assignment2
# @Version:  	20-11-2022
#
# Makefile
##########################################

all: iperf 

clean:
	rm -f iperf iperf.o udp.o 

iperf: udp.o iperf.o
	gcc -g udp.o iperf.o -pthread -lm -o iperf 

udp.o: udp.c
	gcc -c udp.c

iperf.o: iperf.c
	gcc -Wall -ansi -pedantic -c iperf.c
