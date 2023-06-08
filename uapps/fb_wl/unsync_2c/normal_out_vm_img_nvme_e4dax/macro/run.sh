#!/bin/bash

sudo rm -rf /mnt/nvme_shared/*
echo fileserver
sudo filebench -f ./fileserver.f > ./fileserver_0.txt
sudo rm -rf /mnt/nvme_shared/*
sudo filebench -f ./fileserver.f > ./fileserver_1.txt
sudo rm -rf /mnt/nvme_shared/*
sudo filebench -f ./fileserver.f > ./fileserver_2.txt


echo webproxy
sudo filebench -f ./webproxy.f > ./webproxy_0.txt
sudo rm -rf /mnt/nvme_shared/*
sudo filebench -f ./webproxy.f > ./webproxy_1.txt
sudo rm -rf /mnt/nvme_shared/*
sudo filebench -f ./webproxy.f > ./webproxy_2.txt


echo webserver
sudo filebench -f ./webserver.f > ./webserver_0.txt
sudo rm -rf /mnt/nvme_shared/*
sudo filebench -f ./webserver.f > ./webserver_1.txt
sudo rm -rf /mnt/nvme_shared/*
sudo filebench -f ./webserver.f > ./webserver_2.txt


echo varmail
sudo filebench -f ./varmail.f > ./varmail_0.txt
sudo rm -rf /mnt/nvme_shared/*
sudo filebench -f ./varmail.f > ./varmail_1.txt
sudo rm -rf /mnt/nvme_shared/*
sudo filebench -f ./varmail.f > ./varmail_2.txt
sudo rm -rf /mnt/nvme_shared/*
echo done
