echo ""
sudo dmesg -C
sudo sh -c 'echo "3" >> /sys/class/video4linux/video0/dev_debug'
sudo sh -c 'echo "3" >> /sys/module/videobuf2_v4l2/parameters/debug'
sudo sh -c 'echo "3" >> /sys/module/videobuf2_common/parameters/debug'
./test6 -F /dev/video0 --capture=10
echo ""
echo ""
 sudo dmesg
	
	

