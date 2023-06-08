#!/bin/bash


echo fileserver
sudo filebench -f ./fileserver.f > ./fileserver_0.txt
sudo rm -rf /mnt/blk_shared/*
sudo filebench -f ./fileserver.f > ./fileserver_1.txt
sudo rm -rf /mnt/blk_shared/*
sudo filebench -f ./fileserver.f > ./fileserver_2.txt

sudo rm -rf /mnt/blk_shared/*
echo webproxy
sudo filebench -f ./webproxy.f > ./webproxy_0.txt
sudo rm -rf /mnt/blk_shared/*
sudo filebench -f ./webproxy.f > ./webproxy_1.txt
sudo rm -rf /mnt/blk_shared/*
sudo filebench -f ./webproxy.f > ./webproxy_2.txt

sudo rm -rf /mnt/blk_shared/*
echo webserver
sudo filebench -f ./webserver.f > ./webserver_0.txt
sudo rm -rf /mnt/blk_shared/*
sudo filebench -f ./webserver.f > ./webserver_1.txt
sudo rm -rf /mnt/blk_shared/*
sudo filebench -f ./webserver.f > ./webserver_2.txt

sudo rm -rf /mnt/blk_shared/*
echo varmail
sudo filebench -f ./varmail.f > ./varmail_0.txt
sudo rm -rf /mnt/blk_shared/*
sudo filebench -f ./varmail.f > ./varmail_1.txt
sudo rm -rf /mnt/blk_shared/*
sudo filebench -f ./varmail.f > ./varmail_2.txt
sudo rm -rf /mnt/blk_shared/*
echo done
