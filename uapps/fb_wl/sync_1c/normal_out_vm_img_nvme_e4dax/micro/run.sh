#!/bin/bash

sudo rm -rf /mnt/nvme_shared/*
echo rread 1k
sudo filebench -f ./rread_1k.f > ./rread_1k_0.txt
sudo filebench -f ./rread_1k.f > ./rread_1k_1.txt
sudo filebench -f ./rread_1k.f > ./rread_1k_2.txt

sudo rm -rf /mnt/nvme_shared/*
echo rread 4k
sudo filebench -f ./rread_4k.f > ./rread_4k_0.txt
sudo filebench -f ./rread_4k.f > ./rread_4k_1.txt
sudo filebench -f ./rread_4k.f > ./rread_4k_2.txt
sudo rm -rf /mnt/nvme_shared/*

echo rread 16k
sudo filebench -f ./rread_16k.f > ./rread_16k_0.txt
sudo filebench -f ./rread_16k.f > ./rread_16k_1.txt
sudo filebench -f ./rread_16k.f > ./rread_16k_2.txt
sudo rm -rf /mnt/nvme_shared/*

echo rwrite 1k
sudo filebench -f ./rwrite_1k.f > ./rwrite_1k_0.txt
sudo filebench -f ./rwrite_1k.f > ./rwrite_1k_1.txt
sudo filebench -f ./rwrite_1k.f > ./rwrite_1k_2.txt

sudo rm -rf /mnt/nvme_shared/*
echo rwrite 4k
sudo filebench -f ./rwrite_4k.f > ./rwrite_4k_0.txt
sudo filebench -f ./rwrite_4k.f > ./rwrite_4k_1.txt
sudo filebench -f ./rwrite_4k.f > ./rwrite_4k_2.txt

sudo rm -rf /mnt/nvme_shared/*
echo rwrite 16k
sudo filebench -f ./rwrite_16k.f > ./rwrite_16k_0.txt
sudo filebench -f ./rwrite_16k.f > ./rwrite_16k_1.txt
sudo filebench -f ./rwrite_16k.f > ./rwrite_16k_2.txt
sudo rm -rf /mnt/nvme_shared/*

echo sread 1m
sudo filebench -f ./sread_1m.f > ./sread_1m_0.txt
sudo filebench -f ./sread_1m.f > ./sread_1m_1.txt
sudo filebench -f ./sread_1m.f > ./sread_1m_2.txt

sudo rm -rf /mnt/nvme_shared/*
echo sread 4m
sudo filebench -f ./sread_4m.f > ./sread_4m_0.txt
sudo filebench -f ./sread_4m.f > ./sread_4m_1.txt
sudo filebench -f ./sread_4m.f > ./sread_4m_2.txt
sudo rm -rf /mnt/nvme_shared/*

echo sread 8m
sudo filebench -f ./sread_8m.f > ./sread_8m_0.txt
sudo filebench -f ./sread_8m.f > ./sread_8m_1.txt
sudo filebench -f ./sread_8m.f > ./sread_8m_2.txt
sudo rm -rf /mnt/nvme_shared/*

echo swrite 1m
sudo filebench -f ./swrite_1m.f > ./swrite_1m_0.txt
sudo filebench -f ./swrite_1m.f > ./swrite_1m_1.txt
sudo filebench -f ./swrite_1m.f > ./swrite_1m_2.txt

sudo rm -rf /mnt/nvme_shared/*
echo swrite 4m
sudo filebench -f ./swrite_4m.f > ./swrite_4m_0.txt
sudo filebench -f ./swrite_4m.f > ./swrite_4m_1.txt
sudo filebench -f ./swrite_4m.f > ./swrite_4m_2.txt

sudo rm -rf /mnt/nvme_shared/*
echo swrite 8m
sudo filebench -f ./swrite_8m.f > ./swrite_8m_0.txt
sudo filebench -f ./swrite_8m.f > ./swrite_8m_1.txt
sudo filebench -f ./swrite_8m.f > ./swrite_8m_2.txt
sudo rm -rf /mnt/nvme_shared/*
echo done
