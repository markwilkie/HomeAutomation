#! /bin/sh

sudo rm /home/pi/code/rfHub/HubMasterLog.1
sudo mv /home/pi/code/rfHub/HubMasterLog.txt /home/pi/code/rfHub/HubMasterLog.1
sudo rm /home/pi/code/rfHub/RFLog.1
sudo mv /home/pi/code/rfHub/RFLog.txt /home/pi/code/rfHub/RFLog.1
sudo rm /home/pi/code/rfHub/HubMasterStderr.1
sudo mv /home/pi/code/rfHub/HubMasterStderr.txt /home/pi/code/rfHub/HubMasterStderr.1
sudo rm /home/pi/code/rfHub/HubMasterStdout.1
sudo mv /home/pi/code/rfHub/HubMasterStdout.txt /home/pi/code/rfHub/HubMasterStdout.1
sudo /etc/init.d/StartHubMaster.sh start 
