
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
   2.querying the video device capabilities
   3.set the format
   4.set the framerate
   5.allocate  and map buffers
   6.stream on
   7.stream off
   8.close the video device */




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

#define V4L_BUFFERS_DEFAULT	10
#define V4L_BUFFERS_MAX	32

struct buffer
{
	unsigned int size;
	void *mem;
};


struct device
{
	int fd;
	enum v4l2_buf_type type;
	enum v4l2_memory memtype;
	unsigned int nbufs;
	struct buffer *buffers;

	unsigned int width;
	unsigned int height;
	unsigned int bytesperline;
	unsigned int imagesize;

	void *pattern;
	
}dev;


static int test_video_open(struct device *dev, const char *devname, int no_query);
static void test_video_close(struct device *dev);
static int video_set_format(struct device *dev, unsigned int w, unsigned int h, unsigned int format);
static int video_set_framerate(struct device *dev, struct v4l2_fract *time_per_frame);
static int video_prepare_capture(struct device *dev, int nbufs, unsigned int offset,const char *filename);
static int video_load_test_pattern(struct device *dev, const char *filename);
static int video_alloc_buffers(struct device *dev, int nbufs, unsigned int offset);
static int video_queue_buffer(struct device *dev, int index);
static int video_do_capture(struct device *dev, unsigned int nframes,unsigned int skip, unsigned int delay, 
						const char *filename_prefix,int do_requeue_last);



static int test_video_open(struct device *dev, const char *devname, int no_query)
{
	struct v4l2_capability cap;
	int ret;

	
	dev->fd = open(devname, O_RDWR);
	if (dev->fd < 0) {
		printf("Error opening device %s: %d.\n", devname, errno);
		return dev->fd;
	}

	if (!no_query) {
		memset(&cap, 0, sizeof cap);
		printf("test_video_open:VIDIOC_QERYCAP\n");
		ret = ioctl(dev->fd, VIDIOC_QUERYCAP, &cap);
		if (ret < 0) {
			printf("Error opening device %s: unable to query "
				"device.\n", devname);
			close(dev->fd);
			return ret;
		}

		if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
			dev->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		else if (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)
			dev->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		else {
			printf("Error opening device %s: neither video capture "
				"nor video output supported.\n", devname);
			close(dev->fd);
			return -EINVAL;
		}

		printf("Device %s opened: %s (%s).\n", devname, cap.card, cap.bus_info);
	} 
	else {
		dev->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		printf("Device %s opened.\n", devname);
	}

	return 0;
}


static int video_set_format(struct device *dev, unsigned int w, unsigned int h, unsigned int format)
{
	struct v4l2_format fmt;
	int ret;
	
	memset(&fmt, 0, sizeof fmt);
	fmt.type = dev->type;
	fmt.fmt.pix.width = w;
	fmt.fmt.pix.height = h;
	fmt.fmt.pix.pixelformat = format;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;
	printf("video_set_format:VIDIOC_S_FMT\n");
	ret = ioctl(dev->fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("Unable to set format: %s (%d).\n", strerror(errno),
			errno);
		return ret;
	}

	printf("Video format set: width: %u height: %u buffer size: %u\n",
		fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.sizeimage);
	return 0;
}

static int video_set_framerate(struct device *dev, struct v4l2_fract *time_per_frame)
{
	struct v4l2_streamparm parm;
	int ret;

	memset(&parm, 0, sizeof parm);
	parm.type = dev->type;

	printf("Setting frame rate to: %u/%u\n",
		time_per_frame->numerator,
		time_per_frame->denominator);

	parm.parm.capture.timeperframe.numerator = time_per_frame->numerator;
	parm.parm.capture.timeperframe.denominator = time_per_frame->denominator;
	printf("video_set_framerate:VIDIOC_S_PARAM\n");
	ret = ioctl(dev->fd, VIDIOC_S_PARM, &parm);
	if (ret < 0) {
		printf("Unable to set frame rate: %d.\n", errno);
		return ret;
	}
	

	printf("Frame rate set: %u/%u\n",
		parm.parm.capture.timeperframe.numerator,
		parm.parm.capture.timeperframe.denominator);
	return 0;
}

static int video_prepare_capture(struct device *dev, int nbufs, unsigned int offset,
				 const char *filename)
{	
unsigned int i;
	int ret;

	/* Allocate and map buffers. */
	if ((ret = video_alloc_buffers(dev, nbufs, offset)) < 0)
		return ret;

	if (dev->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ret = video_load_test_pattern(dev, filename);
		if (ret < 0)
			return ret;
	}

	/* Queue the buffers. */
	for (i = 0; i < dev->nbufs; ++i) {
		ret = video_queue_buffer(dev, i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int video_alloc_buffers(struct device *dev, int nbufs, unsigned int offset)
{
	struct v4l2_requestbuffers rb;
	struct v4l2_buffer buf;
	int page_size;
	struct buffer *buffers;
	unsigned int i;
	int ret;

	memset(&rb, 0, sizeof rb);
	rb.count = nbufs;
	rb.type = dev->type;
	rb.memory = dev->memtype;
	printf("video_alloc_buffers:VIDIOC_REQBUFS\n");
	ret = ioctl(dev->fd, VIDIOC_REQBUFS, &rb);
	if (ret < 0) {
		printf("Unable to request buffers: %d.\n", errno);
		return ret;
	}

	printf("%u buffers requested.\n", rb.count);

	buffers = malloc(rb.count * sizeof buffers[0]);
	if (buffers == NULL)
		return -ENOMEM;

	page_size = getpagesize();

	/* Map the buffers. */
	for (i = 0; i < rb.count; ++i) {
		memset(&buf, 0, sizeof buf);
		buf.index = i;
		buf.type = dev->type;
		buf.memory = dev->memtype;
		printf("video_alloc_buffers:VIDIOC_QUERYBUF\n");
		ret = ioctl(dev->fd, VIDIOC_QUERYBUF, &buf);
		if (ret < 0) {
			printf("Unable to query buffer %u (%d).\n", i, errno);
			return ret;
		}
		printf("length: %u offset: %u\n", buf.length, buf.m.offset);

		switch (dev->memtype) {
		case V4L2_MEMORY_MMAP:
			buffers[i].mem = mmap(0, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, buf.m.offset);
			if (buffers[i].mem == MAP_FAILED) {
				printf("Unable to map buffer %u (%d)\n", i, errno);
				return ret;
			}
			buffers[i].size = buf.length;
			printf("Buffer %u mapped at address %p.\n", i, buffers[i].mem);
			break;

		case V4L2_MEMORY_USERPTR:
			ret = posix_memalign(&buffers[i].mem, page_size, buf.length + offset);
			if (ret < 0) {
				printf("Unable to allocate buffer %u (%d)\n", i, ret);
				return -ENOMEM;
			}
			buffers[i].mem += offset;
			buffers[i].size = buf.length;
			printf("Buffer %u allocated at address %p.\n", i, buffers[i].mem);
			break;

		default:
			break;
		}
	}

	dev->buffers = buffers;
	dev->nbufs = rb.count;
	return 0;
}

static int video_load_test_pattern(struct device *dev, const char *filename)
{
printf("video_load_test_pattern\n");	
unsigned int size = dev->buffers[0].size;
	unsigned int x, y;
	uint8_t *data;
	int ret;
	int fd;

	/* Load or generate the test pattern */
	dev->pattern = malloc(size);
	if (dev->pattern == NULL)
		return -ENOMEM;

	if (filename == NULL) {
		if (dev->bytesperline == 0) {
			printf("Compressed format detect and no test pattern filename given.\n"
				"The test pattern can't be generated automatically.\n");
			return -EINVAL;
		}

		data = dev->pattern;

		for (y = 0; y < dev->height; ++y) {
			for (x = 0; x < dev->bytesperline; ++x)
				*data++ = x + y;
		}

		return 0;
	}

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		printf("Unable to open test pattern file '%s': %s.\n", filename,
			strerror(errno));
		return -errno;
	}

	ret = read(fd, dev->pattern, size);
	close(fd);

	if (ret != (int)size) {
		printf("Test pattern file size %u doesn't match image size %u\n",
			ret, size);
		return -EINVAL;
	}

	return 0;
}


static int video_queue_buffer(struct device *dev, int index)
{
	struct v4l2_buffer buf;
	int ret;

	memset(&buf, 0, sizeof buf);
	buf.index = index;
	buf.type = dev->type;
	buf.memory = dev->memtype;
	buf.length = dev->buffers[index].size;
	if (dev->memtype == V4L2_MEMORY_USERPTR)
		buf.m.userptr = (unsigned long)dev->buffers[index].mem;

	if (dev->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		buf.bytesused = buf.length;
		memcpy(dev->buffers[buf.index].mem, dev->pattern, buf.bytesused);
	} else
		memset(dev->buffers[buf.index].mem, 0x55, buf.length);
	printf("video_queue_buffer:VIDIOC_QBUF\n");
	ret = ioctl(dev->fd, VIDIOC_QBUF, &buf);
	if (ret < 0)
		printf("Unable to queue buffer (%d).\n", errno);

	return ret;
}


static int video_enable(struct device *dev, int enable)
{
	int type = dev->type;
	int ret;
	printf("video_enable:enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF\n");
	ret = ioctl(dev->fd, enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("Unable to %s streaming: %d.\n", enable ? "start" : "stop",
			errno);
		return ret;
	}

	return 0;
}



static int video_do_capture(struct device *dev, unsigned int nframes,
	unsigned int skip, unsigned int delay, const char *filename_prefix,
	int do_requeue_last)
{
	char *filename = NULL;
	struct timeval start = { 0, 0 };
	struct timeval end, ts;
	struct v4l2_buffer buf;
	unsigned int size;
	unsigned int i;
	FILE *file;
	double bps;
	double fps;
	int ret;

	if (filename_prefix != NULL) {
		filename = malloc(strlen(filename_prefix) + 12);
		if (filename == NULL)
			return -ENOMEM;
	}

	/* Start streaming. */
	video_enable(dev, 1);

	size = 0;

	for (i = 0; i < nframes; ++i) {
		/* Dequeue a buffer. */
		memset(&buf, 0, sizeof buf);
		buf.type = dev->type;
		buf.memory = dev->memtype;
		printf("video_do_capture:VIDIOC_DQBUF\n");
		ret = ioctl(dev->fd, VIDIOC_DQBUF, &buf);
		if (ret < 0) {
			if (errno != EIO) {
				printf("Unable to dequeue buffer (%d).\n", errno);
				free(filename);
				return 1;
			}
			buf.type = dev->type;
			buf.memory = dev->memtype;
			if (dev->memtype == V4L2_MEMORY_USERPTR)
				buf.m.userptr = (unsigned long)dev->buffers[i].mem;
		}

		if (dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
		    dev->imagesize != 0	&& buf.bytesused != dev->imagesize)
			printf("Warning: bytes used %u != image size %u\n",
			       buf.bytesused, dev->imagesize);

		size += buf.bytesused;

		gettimeofday(&ts, NULL);
		printf("%u (%u) [%c] %u %u bytes %ld.%06ld %ld.%06ld\n", i, buf.index,
			(buf.flags & V4L2_BUF_FLAG_ERROR) ? 'E' : '-',
			buf.sequence, buf.bytesused, buf.timestamp.tv_sec,
			buf.timestamp.tv_usec, ts.tv_sec, ts.tv_usec);

		if (i == 0)
			start = ts;

		/* Save the image. */
		if (dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE && filename_prefix && !skip) {
			sprintf(filename, "%s-%06u.bin", filename_prefix, i);
			file = fopen(filename, "wb");
			if (file != NULL) {
				ret = fwrite(dev->buffers[buf.index].mem, buf.bytesused, 1, file);
				fclose(file);
			}
		}
		if (skip)
			--skip;

		/* Requeue the buffer. */
		if (delay > 0)
			usleep(delay * 1000);

		fflush(stdout);

		if (i == nframes - dev->nbufs && !do_requeue_last)
			continue;

		ret = video_queue_buffer(dev, buf.index);
		if (ret < 0) {
			printf("Unable to requeue buffer (%d).\n", errno);
			free(filename);
			return 1;
		}
	}
	gettimeofday(&end, NULL);

	/* Stop streaming. */
	video_enable(dev, 0);

	if (nframes == 0) {
		printf("No frames captured.\n");
		free(filename);
		return 1;
	}

return 0;
}

static void test_video_close(struct device *dev)
{
	
	close(dev->fd);
}

int main(int argc, char *argv[])
{

	struct device dev;
	int ret;
	
	char* name="/dev/video0";
	unsigned int pixelformat = V4L2_PIX_FMT_YUYV;
	unsigned int width = 640;
	unsigned int height = 480;
	struct v4l2_fract time_per_frame = {1,30};
	enum v4l2_memory memtype = V4L2_MEMORY_MMAP;
	unsigned int nbufs = V4L_BUFFERS_DEFAULT;
	unsigned int userptr_offset = 0;
	const char *filename = "/dev/shm/capture.output";
	unsigned int delay = 0, nframes = 10;
	unsigned int skip=0;
	int do_requeue_last = 0;	

	/*open the video device */
	printf("test_video_open\n");
	ret=test_video_open(&dev,name,0);
		if (ret < 0)
			return 1;

	dev.memtype = memtype;

	/* Set the format */	
	printf("video_set_format:\n");	
		ret=video_set_format(&dev, width, height, pixelformat); 
			if(ret < 0){
		printf("video_set_format:before:video_close\n");
			test_video_close(&dev);
			return 1;
		}
	

	/* Set the frame rate */
	printf("video_set_framerate\n");
		ret=video_set_framerate(&dev, &time_per_frame); 
	if(ret < 0){
	printf("video_set_framerate:before:video_close\n");
			test_video_close(&dev);
			return 1;
		}


	/* allocate and map the buffers */
	printf("video_prepare_capture\n");
	ret=video_prepare_capture(&dev, nbufs, userptr_offset, filename);
      	if(ret < 0) {
	printf("video_prepare_capture:video_close\n");
		test_video_close(&dev);
		return 1;
	}

	/* stream on and stream off */
	printf("video_do_capture\n");
	ret=video_do_capture(&dev, nframes, skip, delay, filename, do_requeue_last);
	if(ret < 0){
	printf("video_do_capture:video_close\n");
		test_video_close(&dev);
		return 1;
	}

	/* close the video device */
	printf("test_video_close\n");
	test_video_close(&dev);
	return 0;
}
