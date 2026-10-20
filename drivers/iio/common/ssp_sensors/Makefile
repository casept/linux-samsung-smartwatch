# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for SSP sensor drivers and commons.
#

sensorhub-objs				:= ssp_debugfs.o ssp_dev.o ssp_spi.o
CFLAGS_ssp_spi.o			:= -I$(src)
obj-$(CONFIG_IIO_SSP_SENSORHUB)		+= sensorhub.o

obj-$(CONFIG_IIO_SSP_SENSORS_COMMONS) 	+= ssp_iio.o
