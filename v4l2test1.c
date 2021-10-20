/*
 * yavta --  Yet Another V4L2 Test Application
 *
 * Copyright (C) 2005-2010 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 */


/* 1.open the video device
   2.close the video device */


#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>


#include <linux/videodev2.h>
#include "videodev2-fsr172x.h"

#ifndef V4L2_BUF_FLAG_ERROR
#define V4L2_BUF_FLAG_ERROR	0x0040
#endif

#define ARRAY_SIZE(a)	(sizeof(a)/sizeof((a)[0]))


struct device
{
	int fd;
}dev;


static int test_video_open(struct device *dev,const char *devname, int no_query);
static void test_video_close(struct device *dev);


static int test_video_open(struct device *dev,const char *devname, int no_query)
{
	dev->fd=open(devname, O_RDWR);
	if(dev->fd < 0)
	{
		printf("Error opening device %s: %d\n",devname,errno);
		return dev->fd;
	}
	return 0;
}
static void test_video_close(struct device *dev)
{
	close(dev->fd);
	
}


int main(int argc,char *argv[])
{

	int ret;
	char* name="/dev/video0";

	/* open the video device */
	printf("test_video_open\n");
	ret=test_video_open(&dev,name,0);
		if(ret < 0)
			return 1;

	/* close the video device */
	printf("test_video_close\n");
	test_video_close(&dev);


	return 0;
}
