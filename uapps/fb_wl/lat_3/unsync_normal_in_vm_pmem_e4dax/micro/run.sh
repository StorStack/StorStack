#!/bin/bash

sudo rm -rf /mnt/pmem/*
echo rread 1k
sudo filebench -f ./rread_1k.f > ./rread_1k_0.txt
sudo filebench -f ./rread_1k.f > ./rread_1k_1.txt
sudo filebench -f ./rread_1k.f > ./rread_1k_2.txt

sudo rm -rf /mnt/pmem/*
echo rread 4k
sudo filebench -f ./rread_4k.f > ./rread_4k_0.txt
sudo filebench -f ./rread_4k.f > ./rread_4k_1.txt
sudo filebench -f ./rread_4k.f > ./rread_4k_2.txt

sudo rm -rf /mnt/pmem/*
echo rread 16k
sudo filebench -f ./rread_16k.f > ./rread_16k_0.txt
sudo filebench -f ./rread_16k.f > ./rread_16k_1.txt
sudo filebench -f ./rread_16k.f > ./rread_16k_2.txt

sudo rm -rf /mnt/pmem/*
echo rwrite 1k
sudo filebench -f ./rwrite_1k.f > ./rwrite_1k_0.txt
sudo filebench -f ./rwrite_1k.f > ./rwrite_1k_1.txt
sudo filebench -f ./rwrite_1k.f > ./rwrite_1k_2.txt

sudo rm -rf /mnt/pmem/*
echo rwrite 4k
sudo filebench -f ./rwrite_4k.f > ./rwrite_4k_0.txt
sudo filebench -f ./rwrite_4k.f > ./rwrite_4k_1.txt
sudo filebench -f ./rwrite_4k.f > ./rwrite_4k_2.txt

sudo rm -rf /mnt/pmem/*
echo rwrite 16k
sudo filebench -f ./rwrite_16k.f > ./rwrite_16k_0.txt
sudo filebench -f ./rwrite_16k.f > ./rwrite_16k_1.txt
sudo filebench -f ./rwrite_16k.f > ./rwrite_16k_2.txt

sudo rm -rf /mnt/pmem/*
echo sread 1m
sudo filebench -f ./sread_1m.f > ./sread_1m_0.txt
sudo filebench -f ./sread_1m.f > ./sread_1m_1.txt
sudo filebench -f ./sread_1m.f > ./sread_1m_2.txt

sudo rm -rf /mnt/pmem/*
echo sread 512k
sudo filebench -f ./sread_512k.f > ./sread_512k_0.txt
sudo filebench -f ./sread_512k.f > ./sread_512k_1.txt
sudo filebench -f ./sread_512k.f > ./sread_512k_2.txt

sudo rm -rf /mnt/pmem/*
echo swrite 1m
sudo filebench -f ./swrite_1m.f > ./swrite_1m_0.txt
sudo filebench -f ./swrite_1m.f > ./swrite_1m_1.txt
sudo filebench -f ./swrite_1m.f > ./swrite_1m_2.txt

sudo rm -rf /mnt/pmem/*
echo swrite 512k
sudo filebench -f ./swrite_512k.f > ./swrite_512k_0.txt
sudo filebench -f ./swrite_512k.f > ./swrite_512k_1.txt
sudo filebench -f ./swrite_512k.f > ./swrite_512k_2.txt

echo "done"