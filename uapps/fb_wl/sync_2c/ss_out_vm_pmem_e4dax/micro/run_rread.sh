echo rread 1k
sudo filebench -f ./rread_1k.f > ./rread_1k_0.txt
sudo filebench -f ./rread_1k.f > ./rread_1k_1.txt
sudo filebench -f ./rread_1k.f > ./rread_1k_2.txt


echo rread 4k
sudo filebench -f ./rread_4k.f > ./rread_4k_0.txt
sudo filebench -f ./rread_4k.f > ./rread_4k_1.txt
sudo filebench -f ./rread_4k.f > ./rread_4k_2.txt


echo rread 16k
sudo filebench -f ./rread_16k.f > ./rread_16k_0.txt
sudo filebench -f ./rread_16k.f > ./rread_16k_1.txt
sudo filebench -f ./rread_16k.f > ./rread_16k_2.txt