# Makefile for Fluid Measurement System

CC = g++
CFLAGS = -Wall -std=c++17
LIBS = -lwiringPi -lmicrohttpd -pthread

all: fluid_measurement_server

fluid_measurement_server: fluid_measurement_server.cpp
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f fluid_measurement_server

install:
	mkdir -p /opt/fluid_measurement/web
	cp fluid_measurement_server /opt/fluid_measurement/
	cp -r web/* /opt/fluid_measurement/web/
	cp fluid-measurement.service /etc/systemd/system/
	systemctl daemon-reload
	systemctl enable fluid-measurement.service
	systemctl start fluid-measurement.service

.PHONY: all clean install
