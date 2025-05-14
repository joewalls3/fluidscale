# Makefile for Fluid Measurement System

CC = g++
CFLAGS = -Wall -std=c++17
LIBS = -lwiringPi -lmicrohttpd -pthread

all: fluid_measurement_server

clean:
	rm -f fluid_measurement_server

# Add wifi_setup.cpp to the build
fluid_measurement_server: fluid_measurement_server.cpp wifi_setup.cpp
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Add update_checker.sh to the installation
install:
	mkdir -p /opt/fluid_measurement/web
	cp fluid_measurement_server /opt/fluid_measurement/
	cp update_checker.sh /opt/fluid_measurement/
	chmod +x /opt/fluid_measurement/update_checker.sh
	cp -r web/* /opt/fluid_measurement/web/
	cp fluid-measurement.service /etc/systemd/system/
	systemctl daemon-reload
	systemctl enable fluid-measurement.service
	systemctl start fluid-measurement.service
	(crontab -l 2>/dev/null; echo "0 3 * * * /opt/fluid_measurement/update_checker.sh") | crontab -


.PHONY: all clean install
